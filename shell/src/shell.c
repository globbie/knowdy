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
 *   <http://www.knowdy.net>
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

#include <readline/readline.h>
#include <readline/history.h>

#include "knd_config.h"
#include "knd_shard.h"
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

static int knd_interact(struct kndShard *shard)                      
{
    char *result = malloc(KND_IDX_BUF_SIZE);
    if (!result) return -1;
    size_t result_size = KND_IDX_BUF_SIZE;
    char* buf;
    size_t buf_size;
    int err;

    err = knd_shard_serve(shard);
    if (err) return err;

    knd_log("\n++ Knowdy shard service is up and running, num workers:%zu\n",
            shard->num_tasks);

    printf("   (finish session by pressing Ctrl+C)\n");

    while ((buf = readline(">> ")) != NULL) {
        buf_size = strlen(buf);
        if (buf_size) {
            add_history(buf);
        }
        if (!buf_size) continue;

        printf("[%s :%zu]\n", buf, buf_size);

        err = knd_shard_run_task(shard, buf, buf_size,
                                 result, &result_size);
        if (err != knd_OK) {
            knd_log("-- task run failed");
            goto next_line;
        }

        /* readline allocates a new buffer every time */
    next_line:
        free(buf);
    }

    return knd_OK;
}

/******************* MAIN ***************************/

int main(int argc, char *argv[])
{
    const char *config_filename = NULL;
    char *config_body = NULL;
    size_t config_body_size = 0;
    int long_option;
    struct kndShard *shard;
    int opt;
    int err;

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
        if (fd == -1) goto error;
        fstat(fd, &stat);

        config_body_size = (size_t)stat.st_size;

        config_body = malloc(config_body_size);
        if (!config_body) goto error;

        ssize_t bytes_read = read(fd, config_body, config_body_size);
        if (bytes_read <= 0) goto error;

        close(fd);
    }

    if (!config_body) goto error;

    err = knd_shard_new(&shard, config_body, config_body_size);
    if (err != 0) goto error;

    knd_interact(shard);

 error:
    if (config_body) free(config_body);

    exit(-1);
}
