#include <stdio.h>
#include <stdlib.h>

#include <knd_utils.h>
#include <glb-lib/options.h>
#include <kmq.h>
#include <knd_shard.h>

struct kndLintOptions {
    char *config_file;
} lint_options = {
        .config_file = NULL
};

struct glbOption options[] = {
    {
        .name = "config",
        .name_len = sizeof("config") - 1,
        .short_name = 'c',
        .description = "Config file path.",
        .required = true,
        .data = &lint_options.config_file,
        .type = &kndCStringOptType
    },
    GLB_OPTS_HELP,
    GLB_OPTS_TERMINATOR
};

int main(int argc, const char **argv)
{
    struct kndShard *shard = NULL;
    int err, ret = EXIT_FAILURE;

    err = glb_parse_options(options, argc, argv);
    if (err != 0) {
        knd_log("glb_parse_options() failed, error: '%s'\n", glb_get_options_status());
        return EXIT_FAILURE;
    }

    err = kndShard_new(&shard, lint_options.config_file);
    if (err != 0) goto exit;

    ret = EXIT_SUCCESS;
exit:
    kndShard_del(shard);
    return ret;
}
