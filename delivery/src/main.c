#include "delivery-service.h"

#include <stdio.h>
#include <stdlib.h>

#include <knd_utils.h>
#include <glb-lib/options.h>

static struct kndDeliveryOptions delivery_options = {
    .config_file = NULL
};

struct glbOption options[] = {
    {
        .name = "config",
        .name_len = sizeof("config") - 1,
        .short_name = 'c',
        .description = "Config file path",
        .required = true,
        .data = &delivery_options.config_file,
        .type = &kndCStringOptType
    },
    GLB_OPTS_HELP,
    GLB_OPTS_TERMINATOR
};

int main(int argc, const char **argv)
{
    struct kndDeliveryService *service = NULL;
    int err, ret = EXIT_FAILURE;

    err = glb_parse_options(options, argc, argv);
    if (err != 0) {
        knd_log("glb_parse_options() failed, error: '%s'\n", glb_get_options_status());
        return EXIT_FAILURE;
    }

    err = kndDeliveryService_new(&service, &delivery_options);
    if (err != knd_OK) goto exit;

    err = service->start(service);
    if (err != knd_OK) {
        knd_log("delivery service stopped with failure\n");
        goto exit;
    }

    ret = EXIT_SUCCESS;

exit:
    if (service) service->del(service);
    return ret;
}

