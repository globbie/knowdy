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

#define DEBUG_ATTR_GSL_LEVEL_1 0
#define DEBUG_ATTR_GSL_LEVEL_2 0
#define DEBUG_ATTR_GSL_LEVEL_3 0
#define DEBUG_ATTR_GSL_LEVEL_4 0
#define DEBUG_ATTR_GSL_LEVEL_5 0
#define DEBUG_ATTR_GSL_LEVEL_TMP 1

int knd_attr_export_GSL(struct kndAttr *self, struct kndTask *task, size_t depth)
{
    char buf[KND_NAME_SIZE] = {0};
    size_t buf_size = 0;
    struct kndOutput *out = task->out;
    const char *type_name = knd_attr_names[self->type];
    size_t type_name_size = strlen(knd_attr_names[self->type]);
    size_t indent_size = task->ctx->format_indent;
    int err;

    OUT("{", 1);              
    OUT(type_name, type_name_size);  
    OUT(" ", 1);                     
    OUT(self->name, self->name_size);

    if (self->tr) {
        err = knd_text_gloss_export_GSL(self->tr, true, task, depth + 1);
        KND_TASK_ERR("failed to export attr gloss GSL");
    }

    if (indent_size) {
        OUT("\n", 1);
        err = knd_print_offset(out, (depth + 1) * indent_size);
        RET_ERR();
    }
    if (self->is_a_set) {
        OUT(" {t set}", strlen(" {t set}"));
    }

    if (self->is_implied) {
        OUT(" {impl}", strlen(" {impl}"));
    }

    if (self->is_indexed) {
        OUT(" {idx}", strlen(" {idx}"));
    }

    if (self->concise_level) {
        buf_size = sprintf(buf, "%zu", self->concise_level);
        OUT(" {concise ", strlen(" {concise "));
        OUT(buf, buf_size);
        OUT("}", 1);
    }

    if (self->ref_classname_size) {
        OUT(" {c ", strlen(" {c "));
        OUT(self->ref_classname, self->ref_classname_size);
        OUT("}", 1);
    }

    if (self->ref_proc_name_size) {
        OUT(" {p ", strlen(" {p "));
        OUT(self->ref_proc_name, self->ref_proc_name_size);
        OUT("}", 1);
    }

    OUT("}", 1);

    return knd_OK;
}
