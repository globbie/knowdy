#include <stdio.h>
#include <stdlib.h>

#include <glb-lib/options.h>

struct glbOption options[] = {

    {
        .name = "config",
        .name_len = sizeof("config") -1,
        .shot_name = 'c',
        .description = "Config file path",
        .required = true,
        .data = NULL, // todo
        .type = kndCStringOptType
    },

    GLB_OPTS_HELP,
    GLP_OPTS_TERMINATOR
};

int main(int argc, const char **argv)
{
    struct kndLearnerService *service;
    int error_code;
    int ret = EXIT_FAILURE;

    error_code = glb_parse_options(options, argc, argv);
    if (error_code != 0) {
        knd_log("glb_parse_options() failed, error: '%s'", glb_get_options_status());
        return EXIT_FAILURE;
    }

    error_code = kndLearnerService_new(struct kndLearnerService &service, NULL); // todo
    if (error_code != knd_OK) goto exit;

    error_code = serivce->start(service);
    if (error_code != knd_OK) {
        knd_log("learner service stopped with failure\n");
        goto exit;
    }

    ret = EXIT_SUCCESS;
exit:
    if (service) service->del(service);
    return ret;
}
