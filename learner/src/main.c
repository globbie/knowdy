#include "learner-service.h"

#include <stdio.h>
#include <stdlib.h>

#include <knd_utils.h>
#include <glb-lib/options.h>

char *tmp;

struct glbOption options[] = {

    {
        .name = "config",
        .name_len = sizeof("config") - 1,
        .short_name = 'c',
        .description = "Config file path",
        .required = true,
        .data = &tmp,
        .type = &kndCStringOptType
    },

    GLB_OPTS_HELP,
    GLB_OPTS_TERMINATOR
};


int kndLearnerService_new(struct kndLearnerService **service, void *tmp)
{
    return -1;
}

int main(int argc, const char **argv)
{
    struct kndLearnerService *service = NULL;
    int error_code;
    int ret = EXIT_FAILURE;

    error_code = glb_parse_options(options, argc, argv);
    if (error_code != 0) {
        knd_log("glb_parse_options() failed, error: '%s'", glb_get_options_status());
        return EXIT_FAILURE;
    }

    glb_options_print(options);

    //error_code = kndLearnerService_new(&service, NULL); // todo
    //if (error_code != knd_OK) goto exit;

    //error_code = service->start(service);
    //if (error_code != knd_OK) {
    //    knd_log("learner service stopped with failure\n");
    //    goto exit;
    //}

    ret = EXIT_SUCCESS;
exit:
    if (service) service->del(service);
    return ret;
}
