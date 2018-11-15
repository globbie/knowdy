#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_attr.h"
#include "knd_proc.h"
#include "knd_task.h"
#include "knd_class.h"
#include "knd_text.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_set.h"
#include "knd_mempool.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

#define DEBUG_ATTR_LEVEL_1 0
#define DEBUG_ATTR_LEVEL_2 0
#define DEBUG_ATTR_LEVEL_3 0
#define DEBUG_ATTR_LEVEL_4 0
#define DEBUG_ATTR_LEVEL_5 0
#define DEBUG_ATTR_LEVEL_TMP 1

static void str(struct kndAttr *self)
{
    struct kndTranslation *tr;
    const char *type_name = knd_attr_names[self->type];

    if (self->is_a_set)
        knd_log("\n%*s[%.*s", self->depth * KND_OFFSET_SIZE, "",
                self->name_size, self->name);
    else
        knd_log("\n%*s{%s %.*s", self->depth * KND_OFFSET_SIZE, "",
                type_name, self->name_size, self->name);

    knd_log("%*s  IDX:\"%.*s\" [%zu]",
            self->depth * KND_OFFSET_SIZE, "",
            self->idx_name_size, self->idx_name,
            self->idx_name_size);

    if (self->access_type == KND_ATTR_ACCESS_RESTRICTED) {
        knd_log("%*s  ACL:restricted",
                self->depth * KND_OFFSET_SIZE, "");
    }

    if (self->quant_type == KND_ATTR_SET) {
        knd_log("%*s  QUANT:SET",
                self->depth * KND_OFFSET_SIZE, "");
    }

    if (self->concise_level) {
        knd_log("%*s  CONCISE:%zu",
                self->depth * KND_OFFSET_SIZE, "", self->concise_level);
    }


    if (self->is_implied) {
        knd_log("%*s  (implied)",
                self->depth * KND_OFFSET_SIZE, "");
    }

    tr = self->tr;
    while (tr) {
        knd_log("%*s   ~ %s %s",
                self->depth * KND_OFFSET_SIZE, "", tr->locale, tr->val);
        tr = tr->next;
    }

    if (self->ref_classname_size) {
        knd_log("%*s  REF class template: %.*s",
                self->depth * KND_OFFSET_SIZE, "",
                self->ref_classname_size, self->ref_classname);
    }

    /*if (self->proc) {
        proc = self->proc;
        knd_log("%*s  PROC: %.*s",
                self->depth * KND_OFFSET_SIZE, "", proc->name_size, proc->name);
        proc->depth = self->depth + 1;
        proc->str(proc);
    }
    */
    /*if (self->calc_oper_size) {
        knd_log("%*s  oper: %s attr: %s",
                self->depth * KND_OFFSET_SIZE, "",
                self->calc_oper, self->calc_attr);
    }
    */

    /*if (self->default_val_size) {
        knd_log("%*s  default VAL: %s",
                self->depth * KND_OFFSET_SIZE, "", self->default_val);
    }
    */

    if (self->is_a_set)
        knd_log("%*s]", self->depth * KND_OFFSET_SIZE, "");
    else
        knd_log("%*s}",  self->depth * KND_OFFSET_SIZE, "");
}

extern void str_attr_vars(struct kndAttrVar *item, size_t depth)
{
    struct kndAttrVar *curr_item;
    struct kndAttrVar *list_item;
    const char *classname = "None";
    size_t classname_size = strlen("None");
    struct kndClass *c;
    size_t count = 0;

    if (item->attr && item->attr->is_a_set) {
        c = item->attr->parent_class;
        classname = c->entry->name;
        classname_size = c->entry->name_size;

        knd_log("%*s_list attr: \"%.*s\" (base: %.*s) size: %zu [",
                depth * KND_OFFSET_SIZE, "",
                item->name_size, item->name,
                classname_size, classname,
                item->num_list_elems);
        count = 0;
        if (item->val_size) {
            count = 1;
            knd_log("%*s%zu)  val:%.*s",
                    depth * KND_OFFSET_SIZE, "",
                    count,
                    item->val_size, item->val);
        }

        for (list_item = item->list;
             list_item;
             list_item = list_item->next) {
            count++;
            
            str_attr_vars(list_item, depth + 1);
            
        }
        knd_log("%*s]", depth * KND_OFFSET_SIZE, "");
        //return;
    }

    knd_log("%*s_attr: \"%.*s\" (base: %.*s)  => %.*s",
            depth * KND_OFFSET_SIZE, "",
            item->name_size, item->name,
            classname_size, classname,
            item->val_size, item->val);
    
    if (item->children) {
        for (curr_item = item->children; curr_item; curr_item = curr_item->next) {
            str_attr_vars(curr_item, depth + 1);
        }
    }
}

static int export_JSON(struct kndAttr *self,
                       struct kndTask *task)
{
    struct glbOutput *out = task->out;
    struct kndTranslation *tr;
    struct kndProc *p;
    const char *type_name = knd_attr_names[self->type];
    size_t type_name_size = strlen(knd_attr_names[self->type]);
    int err;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. JSON export attr: \"%.*s\"..",
                self->name_size, self->name);

    err = out->writec(out, '"');
    if (err) return err;
    err = out->write(out, self->name, self->name_size);
    if (err) return err;
    err = out->write(out, "\":{", strlen("\":{"));
    if (err) return err;

    err = out->write(out, "\"type\":\"", strlen("\"type\":\""));
    if (err) return err;
    err = out->write(out, type_name, type_name_size);
    if (err) return err;
    err = out->writec(out, '"');
    if (err) return err;

    if (self->is_a_set) {
        err = out->write(out, ",\"is_a_set\":true", strlen(",\"is_a_set\":true"));
        if (err) return err;
    }

    if (self->ref_classname_size) {
        err = out->write(out, ",\"refclass\":\"", strlen(",\"refclass\":\""));
        if (err) return err;
        err = out->write(out, self->ref_classname, self->ref_classname_size);
        if (err) return err;
        err = out->write(out, "\"", 1);
        if (err) return err;
   }

    /* choose gloss */
    tr = self->tr;
    while (tr) {
        if (DEBUG_ATTR_LEVEL_2)
            knd_log("LANG: %s == CURR LOCALE: %s [%lu] => %s",
                    tr->locale, task->locale,
                    (unsigned long)task->locale_size, tr->val);

        if (strncmp(task->locale, tr->locale, tr->locale_size)) {
            goto next_tr;
        }

        err = out->write(out,
                         ",\"_gloss\":\"", strlen(",\"_gloss\":\""));
        if (err) return err;

        err = out->write(out, tr->val,  tr->val_size);
        if (err) return err;

        err = out->write(out, "\"", 1);
        if (err) return err;
        break;

    next_tr:
        tr = tr->next;
    }

    if (self->proc) {
        err = out->write(out, ",\"proc\":", strlen(",\"proc\":"));
        if (err) return err;
        p = self->proc;
        err = knd_proc_export(p, KND_FORMAT_JSON, out);
        if (err) return err;
    }

    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}

static int export_GSP(struct kndAttr *self, struct glbOutput *out)
{
    char buf[KND_NAME_SIZE] = {0};
    size_t buf_size = 0;
    struct kndTranslation *tr;

    const char *type_name = knd_attr_names[self->type];
    size_t type_name_size = strlen(knd_attr_names[self->type]);
    int err;

    err = out->write(out, "{", 1);
    if (err) return err;
    err = out->write(out, type_name, type_name_size);
    if (err) return err;
    err = out->write(out, " ", 1);
    if (err) return err;
    err = out->write(out, self->name, self->name_size);
    if (err) return err;

    if (self->is_a_set) {
        err = out->write(out, "{t set}", strlen("{t set}"));
        if (err) return err;
    }

    if (self->is_implied) {
        err = out->write(out, "{impl}", strlen("{impl}"));
        if (err) return err;
    }

    if (self->is_indexed) {
        err = out->write(out, "{idx}", strlen("{idx}"));
        if (err) return err;
    }

    if (self->concise_level) {
        buf_size = sprintf(buf, "%zu", self->concise_level);
        err = out->write(out, "{concise ", strlen("{concise "));
        if (err) return err;
        err = out->write(out, buf, buf_size);
        if (err) return err;
        err = out->writec(out, '}');
        if (err) return err;
    }

    if (self->ref_classname_size) {
        err = out->write(out, "{c ", strlen("{c "));
        if (err) return err;
        err = out->write(out, self->ref_classname, self->ref_classname_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;
    }

    if (self->ref_procname_size) {
        err = out->write(out, "{p ", strlen("{p "));
        if (err) return err;
        err = out->write(out, self->ref_procname, self->ref_procname_size);
        if (err) return err;
        err = out->write(out, "}", 1);
        if (err) return err;
    }

    /* choose gloss */
    if (self->tr) {
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
        err = out->write(out, tr->val,  tr->val_size);
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

extern int knd_attr_export(struct kndAttr *self,
                           knd_format format,
                           struct kndTask *task)
{
    switch (format) {
    case KND_FORMAT_JSON: return export_JSON(self, task);
    case KND_FORMAT_GSP:  return export_GSP(self, task->out);
    default:              return knd_NO_MATCH;
    }
}

extern int knd_apply_attr_var_updates(struct kndClass *self,
                                      struct kndClassUpdate *class_update,
                                      struct kndTask *task)
{
    struct kndState *state; //, *s, *next_state = NULL;
    //struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    int err;

    if (DEBUG_ATTR_LEVEL_TMP)
        knd_log(".. applying attr var updates..");

    err = knd_state_new(mempool, &state);
    if (err) return err;

    state->update = class_update->update;
    
    //for (s = self->attr_var_inbox; s; s = next_state) {
        //attr_var = s->val;

        /*knd_log("== attr var %.*s => %.*s",
                attr_var->name_size, attr_var->name,
                s->val_size, s->val); */

        //attr_var->val = s->val;
        //attr_var->val_size = s->val_size;

        //next_state = s->next;
        //s->next = attr_var->states;
        //attr_var->states = s;
        //attr_var->num_states++;
    //}

    state->next = self->states;
    self->states = state;
    self->num_states++;
    state->numid = self->num_states;
    state->phase = KND_UPDATED;

    return knd_OK;
}

extern void kndAttr_init(struct kndAttr *self)
{
    memset(self, 0, sizeof(struct kndAttr));
    self->str = str;
}

extern int knd_copy_attr_ref(void *obj,
                             const char *unused_var(elem_id),
                             size_t unused_var(elem_id_size),
                             size_t unused_var(count),
                             void *elem)
{
    struct kndSet     *attr_idx = obj;
    struct kndAttrRef *src_ref = elem;
    struct kndAttr    *attr    = src_ref->attr;
    struct kndAttrRef *ref;
    struct kndMemPool *mempool = attr_idx->mempool;
    int err;

    if (DEBUG_ATTR_LEVEL_2) 
        knd_log(".. copying %.*s attr..", attr->name_size, attr->name);

    err = knd_attr_ref_new(mempool, &ref);                                        RET_ERR();
    ref->attr = attr;
    ref->attr_var = src_ref->attr_var;
    ref->class_entry = src_ref->class_entry;

    err = attr_idx->add(attr_idx,
                        attr->id, attr->id_size,
                        (void*)ref);                                              RET_ERR();
    return knd_OK;
}

extern int knd_register_attr_ref(void *obj,
                                 const char *unused_var(elem_id),
                                 size_t unused_var(elem_id_size),
                                 size_t unused_var(count),
                                 void *elem)
{
    struct kndClass     *self = obj;
    struct kndSet *attr_idx  = self->attr_idx;
    struct ooDict *attr_name_idx = self->entry->repo->attr_name_idx;
    struct kndAttrRef *src_ref = elem;
    struct kndAttr    *attr    = src_ref->attr;
    struct kndAttrRef *ref, *prev_attr_ref;
    struct kndMemPool *mempool = attr_idx->mempool;
    int err;

    if (DEBUG_ATTR_LEVEL_2) 
        knd_log(".. copying %.*s attr..", attr->name_size, attr->name);

    err = knd_attr_ref_new(mempool, &ref);                                        RET_ERR();
    ref->attr = attr;
    ref->attr_var = src_ref->attr_var;
    ref->class_entry = self->entry;

    err = attr_idx->add(attr_idx,
                        attr->id, attr->id_size,
                        (void*)ref);                                              RET_ERR();

    prev_attr_ref = attr_name_idx->get(attr_name_idx,
                                       attr->name, attr->name_size);

    if (prev_attr_ref) {
        err = attr_name_idx->remove(attr_name_idx,
                                    attr->name, attr->name_size);                     RET_ERR();
        ref->next = prev_attr_ref;
    }

    err = attr_name_idx->set(attr_name_idx,
                             attr->name, attr->name_size,
                             (void*)ref);                                         RET_ERR();
    return knd_OK;
}


static int extract_list_elem_value(struct kndAttrVar *parent_item,
                                   struct kndAttrVar *query,
                                   struct kndProcCallArg *result_arg)
{
    struct kndAttrVar *curr_var;
    int err;

    /* iterate over a list */
    for (curr_var = parent_item->list; curr_var; curr_var = curr_var->next) {
        if (DEBUG_ATTR_LEVEL_2)
            knd_log("== list item:%.*s val: %.*s",
                    curr_var->name_size, curr_var->name,
                    curr_var->val_size, curr_var->val);
        if (!memcmp(curr_var->name, query->name, query->name_size)) {
            err = knd_get_arg_value(curr_var, query->children, result_arg);
            if (err) return err;
            //knd_log("result arg:%zu", result_arg->numval);
        }
    }
    return knd_OK;
}

static int extract_implied_attr_value(struct kndClass *self,
                                      struct kndAttrVar *parent_item,
                                      struct kndProcCallArg *result_arg)
{
    struct kndAttrVar *attr_var;
    struct kndAttrRef *ref;
    void *obj;
    int err;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. from class %.*s get the value of \"%.*s\"..\n",
                self->name_size, self->name,
                parent_item->name_size, parent_item->name);

    /* resolve attr name */
    err = knd_class_get_attr(self,
                             parent_item->name,
                             parent_item->name_size, &ref);
    if (err) return err;
    
    /* get attr var */
    err = self->attr_idx->get(self->attr_idx,
                              ref->attr->id, ref->attr->id_size, &obj);
    if (err) return err;
    ref = obj;

    //knd_log("++ set attr var: %.*s  attr var:%p",
    //        ref->attr->name_size, ref->attr->name, ref->attr_var);
    attr_var = ref->attr_var;

    //str_attr_vars(attr_var, 1);
    //str_attr_vars(parent_item, 1);

    /* go deeper */
    if (parent_item->children) {
        if (attr_var && attr_var->attr->is_a_set) {
            err = extract_list_elem_value(attr_var,
                                          parent_item->children,
                                          result_arg);
            if (err) return err;
        }

        return knd_OK;
    }
    
    return knd_OK;
}

static int compute_attr_var(struct kndAttrVar *parent_item,
                            struct kndAttr *attr,
                            struct kndProcCallArg *result_arg)
{
    struct kndProcCall *proc_call;
    struct kndProcCallArg *arg;
    struct kndClassVar *class_var;
    struct kndAttrVar *attr_var;
    struct kndAttr *curr_attr;
    struct kndAttrRef *ref;
    struct kndClass *template_class = parent_item->attr->ref_class;
    long numval = 0;
    long total = 0;
    long times = 0;
    long quant = 0;
    float dividend = 0;
    float divisor = 0;
    float result = 0;
    int err;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. computing attr \"%.*s\"..", attr->name_size, attr->name);

    proc_call = attr->proc->proc_call;
    for (arg = proc_call->args; arg; arg = arg->next) {
        class_var = arg->class_var;

        if (DEBUG_ATTR_LEVEL_2)
            knd_log("== ARG: %.*s", arg->name_size, arg->name);
        
        for (attr_var = class_var->attrs; attr_var; attr_var = attr_var->next) {
            curr_attr = attr_var->attr;

            if (!attr_var->attr) {
                err = knd_class_get_attr(template_class,
                                         attr_var->name,
                                         attr_var->name_size, &ref);
                if (err) return err;
                attr_var->attr = ref->attr;
                curr_attr = ref->attr;
            }

            if (curr_attr->is_implied) {
                err = extract_implied_attr_value(parent_item->class,
                                                 attr_var->children, arg);
                if (err) return err;
            } else {

                // knd_log("\n  NB:.. regular attr: %.*s",
                //        curr_attr->name_size, curr_attr->name);
                err = knd_get_arg_value(parent_item, attr_var, arg);
                if (err) return err;
            }
        }

        if (!strncmp("total", arg->name, arg->name_size)) {
            total = arg->numval;
        }

        if (!strncmp("times", arg->name, arg->name_size)) {
            times = arg->numval;
            //knd_log("TIMES:%lu", arg->numval);
        }

        if (!strncmp("quant", arg->name, arg->name_size)) {
            quant = arg->numval;
            //knd_log("QUANT:%lu", arg->numval);
        }

        if (!strncmp("dividend", arg->name, arg->name_size)) {
            dividend = (float)arg->numval;
            //knd_log("DIVIDEND:%lu", arg->numval);
        }

        if (!strncmp("divisor", arg->name, arg->name_size)) {
            divisor = (float)arg->numval;
            //knd_log("DIVISOR:%lu (arg:%.*s)", arg->numval,
            //        arg->name_size, arg->name);
        }
    }

    /* run main proc */
    switch (proc_call->type) {
        /* multiplication */
    case KND_PROC_SUM:
        if (total)
            numval = total;
        break;
    case KND_PROC_MULT:
        numval = (times * quant);
        break;
    case KND_PROC_MULT_PERCENT:
        numval = (times * quant) / 100;
        break;
    case KND_PROC_DIV_PERCENT:
        if (!divisor) {
            numval = (long)dividend;
            break;
        }
        result = (dividend / divisor) * (float)100;

        //knd_log("== result: %.2f", result);

        numval = (long)result;
        break;
    default:
        break;
    }
    result_arg->numval = numval;
    return knd_OK;
}

extern int knd_get_arg_value(struct kndAttrVar *src,
                             struct kndAttrVar *query,
                             struct kndProcCallArg *result_arg)
{
    struct kndAttrVar *curr_var;
    struct kndAttr *attr;
    struct kndAttrRef *ref;
    struct kndClass *parent_class = src->class_var->parent;
    int err;

    if (DEBUG_ATTR_LEVEL_2) {
        knd_log("\n\n.. from \"%.*s\" (parent class:%.*s) is_list:%d   extract attr: \"%.*s\"",
                src->name_size, src->name,
                parent_class->name_size, parent_class->name,
                src->attr->is_a_set,
                query->name_size, query->name);
        knd_log("ref class: %.*s",
                src->attr->ref_class->name_size,
                src->attr->ref_class->name);
        str_attr_vars(src, 1);
    }
    
    if (src->attr->ref_class) {
        err = knd_class_get_attr(src->attr->ref_class,
                                 query->name,
                                 query->name_size, &ref);
        if (err) return err;
        attr = ref->attr;

        if (attr->proc) {
            err = compute_attr_var(src, attr, result_arg);
            if (err) return err;
            return knd_OK;
        }
    }
    
    /* check implied attr */
    if (src->implied_attr) {
        attr = src->implied_attr;

        if (!memcmp(attr->name, query->name, query->name_size)) {
            switch (attr->type) {
            case KND_ATTR_NUM:
                if (DEBUG_ATTR_LEVEL_2) {
                    knd_log("== implied NUM attr: %.*s value: %.*s numval:%lu",
                            src->name_size, src->name,
                            src->val_size, src->val, src->numval);
                }
                result_arg->numval = src->numval;
                return knd_OK;
            case KND_ATTR_REF:
                // knd_log("++ match ref: %.*s",
                //        src->class->name_size, src->class->name);

                return knd_get_class_attr_value(src->class, query->children, result_arg);
                break;
            default:
                break;
            }
        }
    }

    /* iterate children */
    for (curr_var = src->children; curr_var; curr_var = curr_var->next) {
        if (DEBUG_ATTR_LEVEL_2)
            knd_log("== child:%.*s val: %.*s",
                    curr_var->name_size, curr_var->name,
                    curr_var->val_size, curr_var->val);


        if (curr_var->name_size != query->name_size) continue;

        if (!strncmp(curr_var->name, query->name, query->name_size)) {

            if (DEBUG_ATTR_LEVEL_2)
                knd_log("++ match: %.*s numval:%zu!\n",
                        curr_var->val_size, curr_var->val, curr_var->numval);

            /* set the implied value */
            if (curr_var->implied_attr) {
                attr = curr_var->implied_attr;
                //knd_log("!! implied attr found in \"%.*s\"!\n\n",
                //        curr_var->name_size, curr_var->name);
                result_arg->numval = curr_var->numval;

                return knd_OK;
            }
            
            result_arg->numval = curr_var->numval;
            if (!query->num_children) return knd_OK;

            //knd_log(".. continue to look up the \"%.*s\" attr..",
            //        query->children->name_size, query->children->name);
            //str_attr_vars(curr_var, 1);
            
            err = knd_get_arg_value(curr_var, query->children, result_arg);
            if (err) return err;
        }
    }
    return knd_OK;
}

extern int knd_attr_var_new(struct kndMemPool *mempool,
                            struct kndAttrVar **result)
{
    void *page;
    int err;
    //knd_log("..attr var new [size:%zu]", sizeof(struct kndAttr));
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                            sizeof(struct kndAttrVar), &page);
    if (err) return err;

    // TEST
    //mempool->num_attr_vars++;

    *result = page;
    return knd_OK;
}

extern int knd_attr_ref_new(struct kndMemPool *mempool,
                            struct kndAttrRef **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndAttrRef), &page);
    if (err) return err;
    *result = page;
    return knd_OK;
}

extern int knd_attr_new(struct kndMemPool *mempool,
                        struct kndAttr **result)
{
    void *page;
    int err;
    //knd_log("..attr new [size:%zu]", sizeof(struct kndAttr));
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                            sizeof(struct kndAttr), &page);
    if (err) return err;
    *result = page;
    kndAttr_init(*result);
    return knd_OK;
}
