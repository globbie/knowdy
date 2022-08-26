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
#include "knd_shared_set.h"
#include "knd_utils.h"
#include "knd_output.h"
#include "knd_http_codes.h"

#define DEBUG_ATTR_GSP_LEVEL_1 0
#define DEBUG_ATTR_GSP_LEVEL_2 0
#define DEBUG_ATTR_GSP_LEVEL_3 0
#define DEBUG_ATTR_GSP_LEVEL_4 0
#define DEBUG_ATTR_GSP_LEVEL_5 0
#define DEBUG_ATTR_GSP_LEVEL_TMP 1

struct LocalContext {
    struct kndClassVar *class_var;
    struct kndAttr     *attr;
    struct kndRepo     *repo;
    struct kndTask     *task;
};


static int export_glosses(struct kndAttr *self, struct kndOutput *out)
{
    char idbuf[KND_ID_SIZE];
    size_t id_size = 0;
    struct kndText *t;
    OUT("[_g", strlen("[_g"));
    FOREACH (t, self->tr) {
        OUT("{", 1);
        OUT(t->locale, t->locale_size);
        OUT("{t ", strlen("{t "));
        knd_uid_create(t->seq->numid, idbuf, &id_size);
        OUT(idbuf, id_size);
        // OUT(t->seq, t->seq_size);
        OUT("}}", 2);
    }
    OUT("]", 1);
    return knd_OK;
}

int knd_attr_export_GSP(struct kndAttr *self, struct kndTask *task)
{
    struct kndOutput *out = task->out;
    char buf[KND_NAME_SIZE] = {0};
    size_t buf_size = 0;

    const char *type_name = knd_attr_names[self->type];
    size_t type_name_size = strlen(knd_attr_names[self->type]);
    int err;

    OUT("{", 1);
    OUT(type_name, type_name_size);

    OUT(" ", 1);
    knd_uid_create(self->seq->numid, buf, &buf_size);
    OUT(buf, buf_size);

    OUT("{id ", strlen("{id "));
    OUT(self->id, self->id_size);
    OUT("}", 1);

    if (self->is_a_set) {
        OUT("{t set}", strlen("{t set}"));
    }

    if (self->is_implied) {
        OUT("{impl}", strlen("{impl}"));
    }

    if (self->is_required) {
        OUT("{req}", strlen("{req}"));
    }

    if (self->is_unique) {
        OUT("{uniq}", strlen("{uniq}"));
    }

    if (self->is_indexed) {
        OUT("{idx}", strlen("{idx}"));
    }

    if (self->concise_level) {
        buf_size = sprintf(buf, "%zu", self->concise_level);
        OUT("{concise ", strlen("{concise "));
        OUT(buf, buf_size);
        OUT("}", 1);
    }

    if (self->ref_class_entry) {
        OUT("{c ", strlen("{c "));
        OUT(self->ref_class_entry->id, self->ref_class_entry->id_size);
        OUT("}", 1);
    }

    if (self->ref_proc_name_size) {
        OUT("{p ", strlen("{p "));
        OUT(self->ref_proc_name, self->ref_proc_name_size);
        OUT("}", 1);
    }

    /* choose gloss */
    if (self->tr) {
        err = export_glosses(self, out);
        KND_TASK_ERR("failed to export glosses GSP");
    }

    OUT("}", 1);
    return knd_OK;
}
