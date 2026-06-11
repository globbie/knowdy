#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

/* numeric conversion by strtol */
#include <errno.h>
#include <limits.h>

#include "knd_config.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_user.h"
#include "knd_repo.h"
#include "knd_mempool.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#define DEBUG_ATTR_JSON_LEVEL_1 0
#define DEBUG_ATTR_JSON_LEVEL_2 0
#define DEBUG_ATTR_JSON_LEVEL_3 0
#define DEBUG_ATTR_JSON_LEVEL_4 0
#define DEBUG_ATTR_JSON_LEVEL_5 0
#define DEBUG_ATTR_JSON_LEVEL_TMP 1

int knd_attr_export_JSON(struct kndAttr *self, struct kndTask *task, size_t depth)
{
    struct kndOutput *out = task->out;
    const char *type_name = knd_attr_names[self->type];
    size_t type_name_size = strlen(knd_attr_names[self->type]);
    int err;

    if (DEBUG_ATTR_JSON_LEVEL_2)
        knd_log(".. JSON export attr: \"%.*s\"", self->name_size, self->name);

    OUT("\"", 1);
    OUT(self->name, self->name_size);
    OUT("\":{", strlen("\":{"));

    OUT("\"type\":\"", strlen("\"type\":\""));
    OUT(type_name, type_name_size);
    OUT("\"", 1);

    switch (self->mult_t) {
    case KND_ATTR_MULTIPLE:
        OUT(",\"mult\":true", strlen(",\"mult\":true"));
        break;
    default:
        break;
    }

#if 0
    if (self->cls_name_size) {
        err = out->write(out, ",\"cls\":\"", strlen(",\"cls\":\""));
        if (err) return err;
        err = out->write(out, self->cls_name, self->cls_name_size);
        if (err) return err;
        err = out->write(out, "\"", 1);
        if (err) return err;
   }
#endif

    err = knd_text_glosses_export_JSON(self->glosses, task, depth + 1);
    KND_TASK_ERR("failed to export attr gloss JSON");

#if 0
    if (self->proc) {
        err = out->write(out, ",\"proc\":", strlen(",\"proc\":"));
        if (err) return err;
        p = self->proc;
        err = knd_proc_export(p, KND_FORMAT_JSON, repo, task, out);
        if (err) return err;
    }
#endif

    OUT("}", 1);
    return knd_OK;
}

