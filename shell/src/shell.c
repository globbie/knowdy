/**
 *   Copyright (c) 2011-present by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the Knowdy Project
 *   and as such it is subject to the license stated
 *   in the LICENSE file which you have received 
 *   as part of this distribution.
 *
 *   Project homepage:
 *   <http://www.knowdy.org>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   ------
 *   shell.c
 *   Knowdy interaction shell 
 */

#include <stdlib.h>
#include <stdio.h>
#include <getopt.h>
#include <string.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <signal.h>

#include <readline/readline.h>
#include <readline/history.h>

#include "knd_config.h"
#include "knd_steward.h"
#include "knd_memblock.h"
#include "knd_user.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_commit.h"
#include "knd_text.h"
#include "knd_utils.h"

#define MEM_TRACE 0
#define KND_MAX_READERS 3
#define KND_MAX_WRITERS 3
#define KND_MAX_COLLECTORS 2

typedef enum oper_t { KND_OPER_DEFAULT,
                      KND_OPER_QUERY,
                      KND_OPER_COMMIT,
                      KND_OPER_UPDATE_STATE
                    } oper_t;

static const char *options_string = "c:h?";

static struct option main_options[] =
{
    {"config", 1, NULL, 'c'},
    {"help", 0, NULL, 'h'},
    { NULL, 0, NULL, 0 }
};

static void display_usage(void)
{
    fprintf(stderr, "\nUsage: knd-shell --config=path_to_your_config\n\n");
}

void sigHandler(int sig_num) 
{
    printf("\n Termination signal received: %s\n", strsignal(sig_num)); 
    fflush(stdout);
    exit(0);
}

static int present_mempools(struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndMemPool *mempool = task->mempool;

    out->reset(out);
    knd_mempool_present(mempool, out);

    knd_log("** Task Main Mempool\n%.*s", out->buf_size, out->buf);

    mempool = task->cache.mempool;

    out->reset(out);
    knd_mempool_present(mempool, out);

    knd_log("** Task Cache Mempool\n%.*s", out->buf_size, out->buf);

    /*out->reset(out);
    mempool = steward->user->mempool_write;
    knd_mempool_present(mempool, out);
    knd_log("** User Space Mempool\n%.*s", out->buf_size, out->buf);
    */
    return knd_OK;
}

static void check_oper_type(const char *req, oper_t *oper_type)
{
    *oper_type = KND_OPER_DEFAULT;

    if (!memcmp(req, "{cmd", strlen("{cmd"))) {
        *oper_type = KND_OPER_COMMIT;
        return;
    }
    if (!memcmp(req, "{query", strlen("{query"))) {
        *oper_type = KND_OPER_QUERY;
        return;
    }
    if (!memcmp(req, "{state", strlen("{state"))) {
        *oper_type = KND_OPER_UPDATE_STATE;
        return;
    }
}

static int create_readers(struct kndSteward *steward, struct kndTask **readers, size_t *total_readers)
{
    struct kndTask *reader_task;
    size_t num_readers = 0;
    int err;

    for (size_t i = 0; i < KND_MAX_READERS; i++) {
        err = knd_task_new(&reader_task, KND_AGENT_READER, i + 1,
                           &steward->mem_task_ctx_config, &steward->mem_task_cache_config, steward);
        if (err) {
            knd_log("failed to create a reader {err %d}", err);
            return err;
        }
        readers[num_readers] = reader_task;
        num_readers++;
    }

    *total_readers = num_readers;
    return knd_OK;
}

static int create_writers(struct kndSteward *steward, struct kndTask **writers,
                          size_t *writer_ids, size_t *total_writers)
{
    struct kndTask *writer_task;
    size_t num_writers = 0;
    int err;

    for (size_t i = 0; i < KND_MAX_WRITERS; i++) {
        err = knd_task_new(&writer_task, KND_AGENT_WRITER, i + 1,
                           &steward->mem_task_ctx_config, &steward->mem_task_cache_config, steward);
        if (err) {
            knd_log("failed to create a writer {err %d}", err);
            return err;
        }
        writers[num_writers] = writer_task;
        writer_ids[num_writers] = writer_task->id;
        num_writers++;
    }
    *total_writers = num_writers;
    return knd_OK;
}

static int create_collectors(struct kndSteward *steward, struct kndTask **collectors, size_t *total_collectors)
{
    struct kndTask *collect_task;
    size_t num_collectors = 0;
    int err;

    for (size_t i = 0; i < KND_MAX_COLLECTORS; i++) {
        err = knd_task_new(&collect_task, KND_AGENT_COLLECTOR, i + 1,
                           &steward->mem_task_ctx_config, &steward->mem_task_cache_config, steward);
        if (err) {
            knd_log("failed to create a collect {err %d}", err);
            return err;
        }
        collectors[num_collectors] = collect_task;
        num_collectors++;
    }
    *total_collectors = num_collectors;
    return knd_OK;
}

static int update_state(struct kndRepoSnapshot *snapshot,
                        struct kndTask **collectors, size_t num_collectors, 
                        size_t *writer_ids, size_t num_writers,
                        struct kndTask *arbiter_task)
{
    assert (num_writers > 0);
    assert (num_writers >= num_collectors);
    struct kndStateLedger *ledger;

    /* distribute writers between collectors */
    size_t batch_size = num_writers / num_collectors;
    size_t remainder = num_writers - (batch_size * num_collectors);
    size_t batch_from = 0;
    size_t batch_to = batch_size + remainder;
    int err;

    knd_log("!! updating global state {num-writers %zu} {num-collectors %zu} ", num_writers, num_collectors);

    err = knd_task_reset(arbiter_task);
    if (err) {
        knd_log("failed to reset the arbiter's task {err %d}", err);
        return err;
    }

    ledger = arbiter_task->ledger;

    /** step one: read available incoming commits 
     *            NB: collectors can run in parallel
     */
    for (size_t i = 0; i < num_collectors; i++) {
        writer_ids += batch_from;

        // TODO specify state range
        err = knd_commit_collect(snapshot, writer_ids, batch_to - batch_from, NULL, ledger, collectors[i]);
        if (err) {
            knd_log("commits collection failure {err %d}", err);
            return err;
        }

        batch_from = batch_to;
        batch_to += batch_size;
    }

    /** step two: index commits to discover intersections / conflicts
     *            NB: lock free concurrency, atomic writes
     */
    for (size_t i = 0; i < num_collectors; i++) {
        err = knd_state_index_commits(snapshot, ledger, collectors[i]);
        if (err) {
            knd_log("commits collection failure {err %d}", err);
            return err;
        }
    }
    
    /**  step three: detect conflicting commits, sort them by rating + timestamp,
     *             schedule non-conflicting commits for the WAL update
     */
    for (size_t i = 0; i < num_collectors; i++) {
        err = knd_state_detect_conflicts(snapshot, ledger, collectors[i]);
        if (err) {
            knd_log("commits conflict detection failure {err %d}", err);
            return err;
        }
    }

    /** step four (final): remaining conflicting commits
     *  must be resolved by the Arbiter
     */


    // TODO get facets
    err = knd_state_resolve_conflicts(snapshot, ledger, arbiter_task);
    if (err) {
        knd_log("commits conflict resolution failure {err %d}", err);
        return err;
    }
    
    /* make sure collectors' WALs are updated */

    // TODO

    /* advance global state */

    return knd_OK;
}

static int knd_interact(struct kndSteward *steward)
{
    struct kndTask *arbiter_task, *curr_task;
    struct kndTask *readers[KND_MAX_READERS] = { 0 };
    size_t num_readers;
    size_t reader_count = 0;
    struct kndTask *writers[KND_MAX_WRITERS] = { 0 };
    size_t writer_ids[KND_MAX_WRITERS] = { 0 };
    size_t num_writers;
    size_t writer_count = 0;
    struct kndTask *collectors[KND_MAX_COLLECTORS] = { 0 };
    size_t num_collectors;

    char  *buf;
    size_t buf_size;
    struct kndMemBlock *memblock = NULL;
    struct kndMemBlock *write_memblock = NULL;
    const char *block;
    size_t block_size;
    const char *steward_role_name = knd_agent_role_names[steward->role];
    struct kndOutput *out = steward->out;
    struct kndOutput *log = steward->log;
    enum oper_t oper_type = 0;
    struct kndRepoSnapshot *snapshot = steward->repo->snapshot;
    int err;

    /* create workers */
    err = knd_task_new(&arbiter_task, KND_AGENT_ARBITER, 42,
                        &steward->mem_task_ctx_config, &steward->mem_task_cache_config, steward);
    KND_STEWARD_ERR("failed to create a writer task");

    err = create_readers(steward, readers, &num_readers);
    KND_STEWARD_ERR("failed to create readers");

    err = create_writers(steward, writers, writer_ids, &num_writers);
    KND_STEWARD_ERR("failed to create writers");

    err = create_collectors(steward, collectors, &num_collectors);
    KND_STEWARD_ERR("failed to create commit collectors");

    /* start serving requests */

    knd_log("\n++ %.*s is up and running!\n"
            "   {steward-role %s}  {knd-version %s}\n",
            steward->name_size, steward->name, steward_role_name, KND_VERSION);
    knd_log("   (finish session by pressing Ctrl+C)\n");

    while ((buf = readline(">> ")) != NULL) {
        buf_size = strlen(buf);
        if (buf_size) {
            add_history(buf);
        }
        if (!buf_size) continue;

        block = buf;
        block_size = buf_size;

        check_oper_type(block, &oper_type);

        switch (oper_type) {
        case KND_OPER_QUERY:
            /* switch between tasks to simulate concurrent reads */
            if (reader_count >= KND_MAX_READERS) {
                reader_count = 0;
            }
            curr_task = readers[reader_count];
            reader_count++;
            break;
        case KND_OPER_COMMIT:
            /* switch between tasks to simulate concurrent writes */
            if (writer_count >= KND_MAX_WRITERS) {
                writer_count = 0;
            }
            curr_task = writers[writer_count];
            writer_count++;
            break;
        case KND_OPER_UPDATE_STATE:
            err = update_state(snapshot, collectors, num_collectors,\
                               writer_ids, num_writers, arbiter_task);
            if (err) {
                knd_log("commits collection failure {err %d}", err);
            }
            goto final;
        default:
            knd_log("-- unsupported operation");
            goto final;
        }

        err = knd_task_reset(curr_task);
        if (err) {
            knd_log("failed to reset a task {err %d}", err);
            goto final;
        }

        err = knd_task_run(curr_task, block, block_size);
        if (err != knd_OK) {
            knd_log("task run failure %.*s", curr_task->output_size, curr_task->output);
        } else {
            knd_log("=== REPLY ===\n%.*s",
                    curr_task->output_size, curr_task->output);
        }

    final:
        /* readline allocates a new buffer every time, need to clean up */
        free(buf);
        memblock = NULL;
        write_memblock = NULL;
    }
    return knd_OK;
}

static int knd_start(const char *config, size_t config_size)
{
    struct kndSteward *steward;
    struct kndResourceReport report;
    struct kndRepoSnapshot *snapshot;
    struct kndTask *task;
    int err;

    err = knd_steward_new(&steward, config, config_size);
    if (err) {
        knd_log("ERR >> failed to create a steward");
        return err;
    }

    task = steward->task;
    knd_log("--");
    present_mempools(task);

    knd_task_monitor(task, steward->active_storage, &report);
    if (report.mem_threshold_alert) {
        knd_log("!! init stage: mem utilization threshold reached");

        err = knd_steward_snapshot_create(steward);
        if (err) goto error;

        err = knd_steward_snapshot_activate(steward, &snapshot);
        if (err) goto error;
    }

    err = knd_interact(steward);
    if (err) goto error;

    knd_steward_del(steward);
    return knd_OK;

 error:
    knd_log("-- %.*s", steward->msg_size, steward->msg);
    knd_steward_del(steward);
    return err;
}

/******************* MAIN ***************************/

int main(int argc, char *argv[])
{
    const char *config_filename = NULL;
    char *config_body = NULL;
    size_t config_body_size = 0;
    int long_option;
    int opt;
    int err;

    signal(SIGINT, sigHandler);

    while ((opt = getopt_long(argc, argv, 
			      options_string, main_options, &long_option)) >= 0) {
	switch (opt) {
	case 'c':
	    if (optarg) {
		config_filename = optarg;
	    }
	    break;
	case 'h':
	case '?':
	    display_usage();
	    break;
	case 0:  /* long option without a short arg */
	    if (!strcmp("config", main_options[long_option].name)) {
		config_filename = optarg;
	    }
	    break;
	default:
	    break;
	}
    }

    if (!config_filename) {
	display_usage();
	goto error;
    }

    { // read config
        struct stat stat;

        int fd = open(config_filename, O_RDONLY);
        if (fd == -1) {
            knd_log("-- failed to open config file \"%s\"", config_filename);
            goto error;
        }
        fstat(fd, &stat);

        config_body_size = (size_t)stat.st_size;

        config_body = malloc(config_body_size);
        if (!config_body) goto error;

        ssize_t bytes_read = read(fd, config_body, config_body_size);
        if (bytes_read <= 0) goto error;
        close(fd);
    }
    if (!config_body) goto error;

    err = knd_start(config_body, config_body_size);
    if (err != 0) goto error;

 error:
    if (config_body) free(config_body);

    exit(-1);
}
