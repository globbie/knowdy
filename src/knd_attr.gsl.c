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
    struct kndText *tr;
    struct kndOutput *out = task->out;
    const char *type_name = knd_attr_names[self->type];
    size_t type_name_size = strlen(knd_attr_names[self->type]);
    int err;

    err = out->write(out, "{", 1);                                                RET_ERR();
    err = out->write(out, type_name, type_name_size);                             RET_ERR();
    err = out->write(out, " ", 1);                                                RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();

    if (self->is_a_set) {
        err = out->write(out, " {t set}", strlen(" {t set}"));
        if (err) return err;
    }

    if (self->is_implied) {
        err = out->write(out, " {impl}", strlen(" {impl}"));
        if (err) return err;
    }

    if (self->is_indexed) {
        err = out->write(out, " {idx}", strlen(" {idx}"));
        if (err) return err;
    }

    if (self->concise_level) {
        buf_size = sprintf(buf, "%zu", self->concise_level);
        err = out->write(out, " {concise ", strlen(" {concise "));
        if (err) return err;
        err = out->write(out, buf, buf_size);
        if (err) return err;
        err = out->writec(out, '}');
        if (err) return err;
    }

    if (self->ref_classname_size) {
        err = out->write(out, " {c ", strlen(" {c "));
        if (err) return err;
        err = out->write(out, self->ref_classname, self->ref_classname_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;
    }

    if (self->ref_procname_size) {
        err = out->write(out, " {p ", strlen(" {p "));
        if (err) return err;
        err = out->write(out, self->ref_procname, self->ref_procname_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;
    }

    /* choose gloss */
    if (self->tr) {
        if (task->ctx->format_offset) {
            err = out->writec(out, '\n');                                         RET_ERR();
            err = knd_print_offset(out, (depth + 1) * task->ctx->format_offset);  RET_ERR();
        }
        err = out->write(out,
                         "[_g", strlen("[_g"));
        if (err) return err;
    }

    for (tr = self->tr; tr; tr = tr->next) {
        err = out->write(out, "{", 1);
        if (err) return err;
        err = out->write(out, tr->locale,  tr->locale_size);
        if (err) return err;
        err = out->write(out, "{t ", 3);
        if (err) return err;
        err = out->write(out, tr->seq->val,  tr->seq->val_size);
        if (err) return err;
        err = out->write(out, "}}", 2);
        if (err) return err;
    }
    if (self->tr) {
        err = out->write(out, "]", 1);
        if (err) return err;
    }

    err = out->write(out, "}", 1);
    if (err) return err;

    return knd_OK;
}
