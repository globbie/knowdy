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
#include "knd_mempool.h"
#include "knd_repo.h"
#include "knd_state.h"
#include "knd_class.h"
#include "knd_class_inst.h"
#include "knd_attr.h"
#include "knd_task.h"
#include "knd_user.h"
#include "knd_text.h"
#include "knd_rel.h"
#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_set.h"
#include "knd_utils.h"
#include "knd_http_codes.h"

#include <gsl-parser.h>
#include <glb-lib/output.h>

#define DEBUG_CLASS_COMP_LEVEL_1 0
#define DEBUG_CLASS_COMP_LEVEL_2 0
#define DEBUG_CLASS_COMP_LEVEL_3 0
#define DEBUG_CLASS_COMP_LEVEL_4 0
#define DEBUG_CLASS_COMP_LEVEL_5 0
#define DEBUG_CLASS_COMP_LEVEL_TMP 1

extern int knd_compute_num_value(struct kndAttr *attr,
                                 struct kndAttrVar *attr_var,
                                 long *result)
{
    struct kndProcCall *proc_call;
    struct kndProcCallArg *arg;
    struct kndClassVar *class_var;
    long numval = 0;
    long times = 0;
    long quant = 0;
    float div_result = 0;
    int err;

    proc_call = attr->proc->proc_call;

    if (DEBUG_CLASS_COMP_LEVEL_TMP)
        knd_log("PROC CALL: \"%.*s\"",
                proc_call->name_size, proc_call->name);

    for (arg = proc_call->args; arg; arg = arg->next) {
        class_var = arg->class_var;

        if (DEBUG_CLASS_COMP_LEVEL_TMP)
            knd_log("ARG: %.*s", arg->name_size, arg->name);

        err = knd_get_arg_value(attr_var, class_var->attrs, arg);
        if (err) return err;

        if (!strncmp("times", arg->name, arg->name_size)) {
            times = arg->numval;
            //knd_log("TIMES:%lu", arg->numval);
        }

        if (!strncmp("quant", arg->name, arg->name_size)) {
            quant = arg->numval;
            //knd_log("QUANT:%lu", arg->numval);
        }
    }

    /* run main proc */
    switch (proc_call->type) {
        /* multiplication */
    case KND_PROC_MULT:
        numval = (times * quant);
        break;
    case KND_PROC_MULT_PERCENT:
        div_result = (float)(times * quant) / (float)100;
        //knd_log("== result: %.2f", div_result);
        numval = (long)div_result;
        break;
    default:
        break;
    }
    
    *result = numval;

    return knd_OK;
}

static int compute_list_sum(struct kndAttrVar *parent_var,
                            struct kndAttrVar *query,
                            struct kndAttr *attr   __attribute__((unused)),
                            struct kndProcCallArg *arg,
                            long *result)
{
    struct kndAttrVar *curr_var;
    struct kndAttrVar *attr_var;
    long total_numval = 0;
    int err;

    if (DEBUG_CLASS_COMP_LEVEL_TMP) {
        knd_log(".. computing SUM of list: %.*s..",
                parent_var->name_size, parent_var->name);
    }

    for (curr_var = parent_var->list; curr_var; curr_var = curr_var->next) {

        if (DEBUG_CLASS_COMP_LEVEL_TMP) {
            knd_log("\n.. list elem: %.*s numval:%lu  ",
                    curr_var->name_size, curr_var->name,
                    curr_var->numval);
        }

        if (query->children) {

            for (attr_var = query->children; attr_var; attr_var = attr_var->next) {
                knd_log(" child query elem: %.*s", attr_var->name_size, attr_var->name);
            }

            if (!curr_var->children) continue;

            err = knd_get_arg_value(curr_var->children, query->children, arg);
            if (err) return err;

            total_numval += arg->numval;
            continue;
        } else {
            //knd_log("== query elem: %.*s", query->name_size, query->name);
            err = knd_get_arg_value(curr_var, query, arg);
            if (err) return err;
            total_numval += arg->numval;
        }

        if (curr_var->numval) {
            total_numval += curr_var->numval;
        }
    }

    *result = total_numval;
    return knd_OK;
}

static int compute_attr_var_value(struct kndClass *self,
                                  struct kndAttrVar *query,
                                  struct kndAttr *attr,
                                  struct kndProcCallArg *arg)
{
    struct kndAttrVar *select_var;
    struct kndAttrVar *field_var;
    struct kndAttrVar *attr_var;
    struct kndAttrRef *ref = NULL;
    void *obj;
    int err;

    if (DEBUG_CLASS_COMP_LEVEL_TMP) {
        knd_log(".. query the \"%.*s\" class..",
                self->name_size, self->name);
    }
    
    // TODO: compute sum of a list
    if (!memcmp("_sum", query->name, query->name_size)) {
        select_var = query->children;
        if (!select_var) return knd_FAIL;

        knd_log("== select attr var:%.*s",
                select_var->name_size, select_var->name);

        field_var = select_var->children;
        if (!field_var) return knd_FAIL;

        /* resolve attr name */
        err = knd_class_get_attr(self,
                                 select_var->name,
                                 select_var->name_size, &ref);
        if (err) return err;

        /* get attr var */
        err = self->attr_idx->get(self->attr_idx,
                                  ref->attr->id, ref->attr->id_size, &obj);
        if (err) return err;

        knd_log("++ set attr var: %.*s", attr->name_size, attr->name);
        ref = obj;
        attr_var = ref->attr_var;

        err = compute_list_sum(ref->attr_var, field_var, attr, arg, &arg->numval);
        if (err) return err;

        // TODO
        //if (!arg->numval) arg->numval = 500;
        knd_log("== SUM: %zu\n", arg->numval);
    }
    return knd_OK;
}

extern int knd_compute_class_attr_num_value(struct kndClass *self,
                                            struct kndAttrVar *attr_var)
{
    struct kndAttr *attr = attr_var->attr;
    struct kndProcCall *proc_call;
    struct kndProcCallArg *arg;
    struct kndClassVar *class_var;
    long numval = 0;
    long times = 0;
    long total = 0;
    long quant = 0;
    float dividend = 0;
    float divisor = 0;
    float result = 0;
    int err;

    proc_call = attr->proc->proc_call;

    if (DEBUG_CLASS_COMP_LEVEL_TMP) {
        knd_log("\nPROC CALL: \"%.*s\" type:%d",
                proc_call->name_size, proc_call->name,
                proc_call->type);

        knd_log("== attr var: \"%.*s\"",
                attr_var->name_size, attr_var->name);
    }

    for (arg = proc_call->args; arg; arg = arg->next) {
        class_var = arg->class_var;

        if (DEBUG_CLASS_COMP_LEVEL_TMP)
            knd_log("ARG: %.*s", arg->name_size, arg->name);

        err = compute_attr_var_value(self,
                                     class_var->attrs, attr, arg);
        if (err) return err;

        if (!strncmp("total", arg->name, arg->name_size)) {
            total = arg->numval;
            //knd_log("TIMES:%lu", arg->numval);
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
        knd_log(".. divide.. ");
        if (!divisor) {
            numval = (long)dividend;
            break;
        }

        knd_log("\n.. divide %.2f by %.2f", dividend, divisor);
        result = (dividend / divisor) * (float)100;

        knd_log("== result: %.2f", result);

        numval = (long)result;
        break;
    default:
        break;
    }
    attr_var->numval = numval;
    return knd_OK;
}



