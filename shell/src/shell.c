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
#include "knd_text.h"
#include "knd_utils.h"

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

static int check_file_rec(struct kndTask *task, const char *rec, size_t rec_size,
                          struct kndMemBlock **result) 
{
    char buf[1024];
    size_t buf_size;
    size_t prefix_size = strlen("{file ");
    struct stat st;
    struct kndMemBlock *memblock;
    int err;

    if (memcmp(rec, "{file ", prefix_size)) {
        return knd_OK;
    }
    rec += prefix_size;
    buf_size = rec_size - prefix_size - 1; 
    memcpy(buf, rec, buf_size);
    buf[buf_size] = '\0';

    knd_log(".. reading input file \"%.*s\"", buf_size, buf);

    if (stat(buf, &st)) {
        knd_log("-- no such file: \"%.*s\"", buf_size, buf);
        return knd_FAIL;
    }
    if ((size_t)st.st_size >= 1024 * 1024) {
        err = knd_LIMIT;
        KND_TASK_ERR("max input file size limit reached");
    }

    err = knd_memblock_new(&memblock, 0, (size_t)st.st_size);
    KND_TASK_ERR("failed to alloc a memblock");
    
    err = knd_memblock_read_file(memblock, buf, (size_t)st.st_size);
    KND_TASK_ERR("failed to read memblock from {file %.*s}", buf_size, buf);

    *result = memblock;
    return knd_OK;
}

static int knd_interact(struct kndSteward *steward)
{
    struct kndTask *reader_task;
    struct kndTask *writer_task;
    char  *buf;
    size_t buf_size;
    struct kndMemBlock *memblock = NULL;
    struct kndMemBlock *write_memblock = NULL;
    const char *block;
    size_t block_size;
    const char *steward_role_name = knd_agent_role_names[steward->role];
    struct kndOutput *out = steward->out;
    struct kndOutput *log = steward->log;
    struct kndResourceReport report;
    int err;

    err = knd_task_new(&writer_task, KND_AGENT_ARBITER, 1, steward);
    KND_STEWARD_ERR("failed to create a writer/arbiter task");

    err = knd_task_new(&reader_task, KND_AGENT_READER, 2, steward);
    KND_STEWARD_ERR("failed to create a reader task");

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

        // printf("[%s :%zu]\n", buf, buf_size);
        block = buf;
        block_size = buf_size;

        /*if (block[0] == '[') {
            knd_task_reset(reader_task);
            err = knd_text_build_JSON(block, block_size, reader_task);
            if (err) goto next_line;
            knd_log("== JSON: %.*s", reader_task->output_size, reader_task->output);
            }*/

        err = check_file_rec(reader_task, buf, buf_size, &memblock);
        if (err) goto next_line;
        if (memblock) {
            block = memblock->buf;
            block_size = memblock->buf_size;
        }

        /* reader task is always the first to parse and validate the request */
        knd_task_reset(reader_task);
        reader_task->ctx->max_depth = 3;
        // reader_task->mode = KND_TASK_TRACE_MODE;
        
        err = knd_task_run(reader_task, block, block_size);
        if (err != knd_OK) {
            knd_log("-- task run failed: %.*s",
                    reader_task->output_size, reader_task->output);
            goto next_line;
        }

        knd_log("=== REPLY ===\n\n%.*s", reader_task->output_size, reader_task->output);

        // out->reset(out);
        // reader_task->mempool->present(reader_task->mempool, out);
        // knd_log("** Task Mempool (%p)\n%.*s", reader_task->mempool, out->buf_size, out->buf);

        /* writing tasks require another run,
           possibly involving network communication */
        switch (reader_task->ctx->phase) {
        case KND_CONFIRM_COMMIT:
            err = knd_memblock_new(&write_memblock, 0, reader_task->output_size);
            if (err) goto next_line;

            err = knd_memblock_copy(write_memblock, reader_task->output, reader_task->output_size);
            if (err != knd_OK) {
                knd_log("-- update block allocation failed");
                goto next_line;
            }
            
            knd_task_reset(writer_task);
            err = knd_task_run(writer_task, write_memblock->buf, write_memblock->buf_size);
            if (err != knd_OK) {
                knd_log("-- update confirm failed: %.*s",
                        writer_task->output_size, writer_task->output);
                goto next_line;
            }
            knd_log("== Arbiter's output:\n%.*s",
                    writer_task->output_size, writer_task->output);

            /* check system resource utilization */
            knd_steward_monitor(steward, &report);
            if (report.mem_threshold_alert) {
                knd_log("!! mem utilization threshold reached");

                /* build an on-disk snapshot up to the latest commit number */
                err = knd_steward_snapshot_create(steward);
                KND_STEWARD_ERR("failed to build an on-disk snapshot");

                /* suspend all writing tasks */

                err = knd_steward_snapshot_activate(steward);
                KND_STEWARD_ERR("steward cleanup failed");

                /* re-initialize all writing tasks */
                knd_task_cleanup(writer_task, steward);
            }
            break;
        default:
            break;
        }
        
        /* readline allocates a new buffer every time */
    next_line:
        free(buf);
        memblock = NULL;
        write_memblock = NULL;
    }
    return knd_OK;
}

static int present_mempools(struct kndSteward *steward)
{
    struct kndOutput *out;
    struct kndMemPool *mempool;

    out = steward->task->out;
    out->reset(out);
    mempool = steward->mempool_write;
    knd_mempool_present(mempool, out);
    knd_log("** System Mempool\n%.*s", out->buf_size, out->buf);

    out->reset(out);
    mempool = steward->user->mempool_write;
    knd_mempool_present(mempool, out);
    knd_log("** User Space Mempool\n%.*s", out->buf_size, out->buf);
    return knd_OK;
}

static int knd_start(const char *config, size_t config_size)
{
    struct kndSteward *steward;
    struct kndResourceReport report;
    int err;

    err = knd_steward_new(&steward, config, config_size);
    if (err) {
        knd_log("ERR >> failed to create a steward");
        return err;
    }
    present_mempools(steward);

    knd_steward_monitor(steward, &report);
    if (report.mem_threshold_alert) {
        knd_log("!! init stage: mem utilization threshold reached");

        err = knd_steward_snapshot_create(steward);
        if (err) goto error;

        err = knd_steward_snapshot_activate(steward);
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
