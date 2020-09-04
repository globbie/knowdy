#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_attr.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_proc_call.h"
#include "knd_task.h"
#include "knd_class.h"
#include "knd_text.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_set.h"
#include "knd_mempool.h"
#include "knd_output.h"

#include <gsl-parser.h>

#define DEBUG_ATTR_LEVEL_1 0
#define DEBUG_ATTR_LEVEL_2 0
#define DEBUG_ATTR_LEVEL_3 0
#define DEBUG_ATTR_LEVEL_4 0
#define DEBUG_ATTR_LEVEL_5 0
#define DEBUG_ATTR_LEVEL_TMP 1

void knd_attr_str(struct kndAttr *self, size_t depth)
{
    struct kndText *tr;
    const char *type_name = knd_attr_names[self->type];

    if (self->is_a_set)
        knd_log("\n%*s[%.*s", depth * KND_OFFSET_SIZE, "",
                self->name_size, self->name);
    else
        knd_log("\n%*s{%s %.*s", depth * KND_OFFSET_SIZE, "",
                type_name, self->name_size, self->name);

    if (self->quant_type == KND_ATTR_SET) {
        knd_log("%*s  QUANT:SET",
                depth * KND_OFFSET_SIZE, "");
    }

    if (self->concise_level) {
        knd_log("%*s  CONCISE:%zu",
                depth * KND_OFFSET_SIZE, "", self->concise_level);
    }


    if (self->is_implied) {
        knd_log("%*s  (implied)",
                depth * KND_OFFSET_SIZE, "");
    }

    tr = self->tr;
    while (tr) {
        knd_log("%*s   ~ %s %.*s",
                depth * KND_OFFSET_SIZE, "", tr->locale, tr->seq_size, tr->seq);
        tr = tr->next;
    }

    if (self->ref_classname_size) {
        knd_log("%*s  REF class template: %.*s",
                depth * KND_OFFSET_SIZE, "",
                self->ref_classname_size, self->ref_classname);
    }

    /*if (self->proc) {
        proc = self->proc;
        knd_log("%*s  PROC: %.*s",
                depth * KND_OFFSET_SIZE, "", proc->name_size, proc->name);
        proc->depth = depth + 1;
        proc->str(proc);
    }
    */
    /*if (self->calc_oper_size) {
        knd_log("%*s  oper: %s attr: %s",
                depth * KND_OFFSET_SIZE, "",
                self->calc_oper, self->calc_attr);
    }
    */

    /*if (self->default_val_size) {
        knd_log("%*s  default VAL: %s",
                depth * KND_OFFSET_SIZE, "", self->default_val);
    }
    */

    if (self->is_a_set)
        knd_log("%*s]", depth * KND_OFFSET_SIZE, "");
    else
        knd_log("%*s}",  depth * KND_OFFSET_SIZE, "");
}

void knd_attr_var_str(struct kndAttrVar *var, size_t depth)
{
    assert(var->attr != NULL);

    struct kndAttr *attr = var->attr;
    struct kndAttrVar *item;
    const char *type_name = "";

    type_name = knd_attr_names[attr->type];
    if (var->is_list_item) {
        switch (attr->type) {
        case KND_ATTR_INNER:
            knd_log("%*s* inner \"%.*s\":", depth * KND_OFFSET_SIZE, "",
                    attr->ref_class->name_size, attr->ref_class->name);
            break;
        case KND_ATTR_REF:
            knd_log("%*s* ref \"%.*s\":", depth * KND_OFFSET_SIZE, "",
                    attr->ref_class->name_size, attr->ref_class->name);
            break;
        default:
            knd_log("%*s* %s: \"%.*s\"", depth * KND_OFFSET_SIZE, "",
                    type_name, var->name_size, var->name);
            break;
        }
        if (var->implied_attr) {
            type_name = knd_attr_names[var->implied_attr->type];
            knd_log("%*s_implied: \"%.*s\" (%s) => %.*s", (depth + 1) * KND_OFFSET_SIZE, "",
                    var->implied_attr->name_size, var->implied_attr->name,
                    type_name, var->val_size, var->val);
        }
        for (item = var->children; item; item = item->next)
            knd_attr_var_str(item, depth + 1);
        return;
    }

    if (attr->is_a_set) {
        knd_log("%*s%.*s (%s)  [", depth * KND_OFFSET_SIZE, "", var->name_size, var->name, type_name);

        for (item = var->list; item; item = item->next)
            knd_attr_var_str(item, depth + 1);

        knd_log("%*s]", depth * KND_OFFSET_SIZE, "");
        return;
    }

    switch (attr->type) {
        case KND_ATTR_INNER:
            knd_log("%*s%.*s (inner \"%.*s\")", depth * KND_OFFSET_SIZE, "",
                    var->name_size, var->name,
                    attr->ref_class->name_size, attr->ref_class->name);
            break;
        case KND_ATTR_REF:
            knd_log("%*s%.*s (\"%.*s\" class ref) => %.*s", depth * KND_OFFSET_SIZE, "",
                    var->name_size, var->name,
                    attr->ref_class->name_size, attr->ref_class->name,
                    var->class->name_size, var->class->name);
            break;
        case KND_ATTR_REL:
            knd_log("%*s%.*s (\"%.*s\" class inst rel) => \"%.*s\"", depth * KND_OFFSET_SIZE, "",
                    var->name_size, var->name,
                    attr->ref_class->name_size, attr->ref_class->name,
                    var->class_inst_entry->name_size, var->class_inst_entry->name);
            break;
        case KND_ATTR_TEXT:
            knd_log("%*s%.*s:", depth * KND_OFFSET_SIZE, "", var->name_size, var->name);
            knd_text_str(var->text, depth + 1);
            return;
        default:
            knd_log("%*s%.*s (%s) => %.*s", depth * KND_OFFSET_SIZE, "",
                   var->name_size, var->name,  type_name, var->val_size, var->val);
            break;
    }

    for (item = var->children; item; item = item->next)
        knd_attr_var_str(item, depth + 1);
}

static int export_JSON(struct kndAttr *self,
                       struct kndTask *task)
{
    struct kndOutput *out = task->out;
    struct kndText *tr;
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
        if (task->ctx->locale_size != tr->locale_size) continue;
        if (memcmp(task->ctx->locale, tr->locale, tr->locale_size)) {
            goto next_tr;
        }

        err = out->write(out,
                         ",\"_gloss\":\"", strlen(",\"_gloss\":\""));
        if (err) return err;

        err = out->write(out, tr->seq,  tr->seq_size);
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
        err = knd_proc_export(p, KND_FORMAT_JSON, task, out);
        if (err) return err;
    }

    err = out->writec(out, '}');
    if (err) return err;

    return knd_OK;
}

static int export_GSP(struct kndAttr *self, struct kndOutput *out)
{
    char buf[KND_NAME_SIZE] = {0};
    size_t buf_size = 0;
    struct kndText *tr;

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

    if (self->is_required) {
        err = out->write(out, "{req}", strlen("{req}"));
        if (err) return err;
    }

    if (self->is_unique) {
        err = out->write(out, "{uniq}", strlen("{uniq}"));
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
        err = out->write(out, tr->seq,  tr->seq_size);
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

int knd_attr_export(struct kndAttr *self, knd_format format, struct kndTask *task)
{
    switch (format) {
    case KND_FORMAT_JSON: return export_JSON(self, task);
    case KND_FORMAT_GSP:  return export_GSP(self, task->out);
    default:              return knd_NO_MATCH;
    }
}

int knd_apply_attr_var_commits(struct kndClass *unused_var(self), struct kndClassCommit *class_commit, struct kndTask *task)
{
    struct kndState *state; //, *s, *next_state = NULL;
    //struct kndAttrVar *attr_var;
    struct kndMemPool *mempool = task->mempool;
    int err;

    if (DEBUG_ATTR_LEVEL_TMP)
        knd_log(".. applying attr var commits..");

    err = knd_state_new(mempool, &state);
    if (err) return err;

    state->commit = class_commit->commit;
    
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

    /* state->next = self->states;
    self->states = state;
    self->num_states++;
    state->numid = self->num_states;
    state->phase = KND_UPDATED;
    */
    return knd_OK;
}

void kndAttr_init(struct kndAttr *self)
{
    memset(self, 0, sizeof(struct kndAttr));
}

extern int knd_register_attr_ref(void *obj,
                                 const char *unused_var(elem_id),
                                 size_t unused_var(elem_id_size),
                                 size_t unused_var(count),
                                 void *elem)
{
    struct kndClass *self = obj;
    struct kndSet *attr_idx  = self->attr_idx;
    struct kndSharedDict *attr_name_idx = self->entry->repo->attr_name_idx;
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

    prev_attr_ref = knd_shared_dict_get(attr_name_idx,
                                        attr->name, attr->name_size);

    ref->next = prev_attr_ref;
    err = knd_shared_dict_set(attr_name_idx,
                              attr->name, attr->name_size,
                              (void*)ref,
                              mempool,
                              NULL, NULL, true);                                  RET_ERR();

    return knd_OK;
}

static int compute_attr_var(struct kndAttrVar *unused_var(parent_item),
                            struct kndAttr *attr,
                            struct kndProcCallArg *result_arg)
{
    struct kndProcCall *proc_call;
    //struct kndProcCallArg *arg;
    //struct kndAttrVar *attr_var;
    //struct kndAttr *curr_attr;
    //struct kndAttrRef *ref;
    //struct kndClass *template_class = parent_item->attr->ref_class;
    long numval = 0;
    long total = 0;
    long times = 0;
    long quant = 0;
    float dividend = 0;
    float divisor = 0;
    float result = 0;
    //int err;

    if (DEBUG_ATTR_LEVEL_2)
        knd_log(".. computing attr \"%.*s\"..", attr->name_size, attr->name);

    // TODO: proc call list
    proc_call = attr->proc->calls;

    /*for (arg = proc_call->args; arg; arg = arg->next) {
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
    */

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
        knd_attr_var_str(src, 1);
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
            //knd_attr_var_str(curr_var, 1);
            
            err = knd_get_arg_value(curr_var, query->children, result_arg);
            if (err) return err;
        }
    }
    return knd_OK;
}

int knd_attr_var_new(struct kndMemPool *mempool, struct kndAttrVar **result)
{
    void *page;
    int err;
    assert(mempool->small_x2_page_size >= sizeof(struct kndAttrVar));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X2, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndAttrVar));
    *result = page;
    return knd_OK;
}

int knd_attr_idx_new(struct kndMemPool *mempool, struct kndAttrIdx **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndAttrIdx));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndAttrIdx));
    *result = page;
    return knd_OK;
}

int knd_attr_facet_new(struct kndMemPool *mempool, struct kndAttrFacet **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndAttrFacet));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndAttrFacet));
    *result = page;
    return knd_OK;
}

int knd_attr_hub_new(struct kndMemPool *mempool, struct kndAttrHub **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndAttrHub));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndAttrHub));
    *result = page;
    return knd_OK;
}

int knd_attr_ref_new(struct kndMemPool *mempool, struct kndAttrRef **result)
{
    void *page;
    int err;
    assert(mempool->tiny_page_size >= sizeof(struct kndAttrRef));
    err = knd_mempool_page(mempool, KND_MEMPAGE_TINY, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndAttrRef));
    *result = page;
    return knd_OK;
}

int knd_attr_new(struct kndMemPool *mempool, struct kndAttr **result)
{
    void *page;
    int err;
    assert(mempool->small_x2_page_size >= sizeof(struct kndAttr));
    err = knd_mempool_page(mempool, KND_MEMPAGE_SMALL_X2, &page);
    if (err) return err;
    memset(page, 0,  sizeof(struct kndAttr));
    *result = page;
    kndAttr_init(*result);
    return knd_OK;
}
