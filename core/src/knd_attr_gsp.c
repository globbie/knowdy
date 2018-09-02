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
#include "knd_http_codes.h"

#include <glb-lib/output.h>

#define DEBUG_ATTR_GSP_LEVEL_1 0
#define DEBUG_ATTR_GSP_LEVEL_2 0
#define DEBUG_ATTR_GSP_LEVEL_3 0
#define DEBUG_ATTR_GSP_LEVEL_4 0
#define DEBUG_ATTR_GSP_LEVEL_5 0
#define DEBUG_ATTR_GSP_LEVEL_TMP 1

static int ref_item_export_GSP(struct kndAttrVar *item,
                               struct glbOutput *out)
{
    struct kndClass *c;
    int err;

    assert(item->class != NULL);
    c = item->class;
    //err = out->write(out, c->name, c->name_size);                                 RET_ERR();
    err = out->write(out, c->entry->id, c->entry->id_size);                       RET_ERR();
    //err = out->writec(out, '}');                                                  RET_ERR();

    return knd_OK;
}

static int aggr_item_export_GSP(struct kndAttrVar *parent_item,
                                struct glbOutput *out)
{
    struct kndAttrVar *item;
    struct kndAttr *attr;
    struct kndClass *c;
    int err;

    c = parent_item->attr->parent_class;

    if (DEBUG_ATTR_GSP_LEVEL_TMP) {
        knd_log(".. GSP export aggr item: %.*s (id:%.*s)",
                parent_item->name_size, parent_item->name,
                parent_item->id_size, parent_item->id);
    }

    /*if (parent_item->id_size) {
        err = out->write(out, parent_item->id, parent_item->id_size);
        if (err) return err;
        }*/

    if (parent_item->val_size) {
        err = out->write(out, parent_item->val, parent_item->val_size);
        if (err) return err;
    }
    
    for (item = parent_item->children; item; item = item->next) {
        err = out->writec(out, '{');                                              RET_ERR();

        err = out->write(out, item->name, item->name_size);                       RET_ERR();
        err = out->writec(out, ' ');                                              RET_ERR();

        switch (item->attr->type) {
        case KND_ATTR_REF:
            err = ref_item_export_GSP(item, out);                                 RET_ERR();
            break;
        case KND_ATTR_AGGR:
            err = aggr_item_export_GSP(item, out);                                RET_ERR();
            break;
        default:
            err = out->write(out, item->val, item->val_size);                     RET_ERR();
            break;
        }
        err = out->writec(out, '}');                                              RET_ERR();
    }

    return knd_OK;
}


static int proc_item_export_GSP(struct kndAttrVar *item)
{
    struct kndProc *proc;
    int err;

    assert(item->proc != NULL);

    proc = item->proc;
    err = proc->export(proc);
    if (err) return err;

    return knd_OK;
}

static int attr_var_list_export_GSP(struct kndAttrVar *parent_item,
                                    struct glbOutput *out)
{
    struct kndAttrVar *item;
    struct kndAttr *attr;
    struct kndClass *c;
    size_t count = 0;
    int err;

    if (DEBUG_ATTR_GSP_LEVEL_2) {
        knd_log(".. export GSP list: %.*s\n\n",
                parent_item->name_size, parent_item->name);
    }

    err = out->writec(out, '[');                                                  RET_ERR();
    err = out->writec(out, '!');                                                  RET_ERR();

    err = out->write(out, parent_item->name, parent_item->name_size);             RET_ERR();

    for (item = parent_item->list; item; item = item->next) {
        err = out->writec(out, '{');                                              RET_ERR();
        switch (item->attr->type) {
        case KND_ATTR_REF:
            c = item->class;
            err = out->write(out,
                             c->entry->id,
                             c->entry->id_size);                        RET_ERR();
            break;
        case KND_ATTR_AGGR:
            /* check implied field */
            
            if (item->class) {
                c = item->class;
                err = out->write(out,
                                 c->entry->id,
                                 c->entry->id_size);                              RET_ERR();
            } else {
                err = out->write(out,
                                 item->name,
                                 item->name_size);                                RET_ERR();
            }
            err = knd_attr_var_export_GSP(item, out);                             RET_ERR();
            break;
        default:
            err = out->write(out,
                             item->name,
                             item->name_size);                                    RET_ERR();
            err = knd_attr_var_export_GSP(item, out);                             RET_ERR();
        }
        err = out->writec(out, '}');                                              RET_ERR();
    }
    err = out->writec(out, ']');                                                  RET_ERR();

    return knd_OK;
}

extern int knd_attr_vars_export_GSP(struct kndAttrVar *items,
                                     struct glbOutput *out,
                                     size_t depth __attribute__((unused)),
                                     bool is_concise)
{
    struct kndAttrVar *item;
    struct kndAttr *attr;
    struct kndClass *c;
    int err;

    for (item = items; item; item = item->next) {
        if (!item->attr) continue;
        attr = item->attr;
        if (is_concise && !attr->concise_level) continue;

        if (attr->is_a_set) {
            err = attr_var_list_export_GSP(item, out);
            if (err) return err;
            continue;
        }

        err = out->writec(out, '{');                                                  RET_ERR();
        err = out->write(out, item->name, item->name_size);                           RET_ERR();
        err = out->writec(out, ' ');                                                  RET_ERR();
        err = knd_attr_var_export_GSP(item, out);                                     RET_ERR();
        err = out->writec(out, '}');                                                  RET_ERR();
    }
    return knd_OK;
}

extern int knd_attr_var_export_GSP(struct kndAttrVar *item,
                                    struct glbOutput *out)
{
    struct kndClass *c;
    int err;
    
    switch (item->attr->type) {
    case KND_ATTR_NUM:
        err = out->write(out, item->val, item->val_size);                         RET_ERR();
        break;
    case KND_ATTR_REF:
        //err = ref_item_export_GSP(item, out);
        //if (err) return err;
        break;
    case KND_ATTR_PROC:
        err = proc_item_export_GSP(item);                                         RET_ERR();
        break;
    case KND_ATTR_AGGR:
        err = aggr_item_export_GSP(item, out);                                    RET_ERR();
        break;
    default:
        err = out->write(out, item->val, item->val_size);                     RET_ERR();
    }
    
    return knd_OK;
}
