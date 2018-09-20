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

    proc_call = &attr->proc->proc_call;

    if (DEBUG_CLASS_COMP_LEVEL_2)
        knd_log("PROC CALL: \"%.*s\"",
                proc_call->name_size, proc_call->name);

    for (arg = proc_call->args; arg; arg = arg->next) {
        class_var = arg->class_var;

        if (DEBUG_CLASS_COMP_LEVEL_2)
            knd_log("ARG: %.*s", arg->name_size, arg->name);

        err = get_arg_value(attr_var, class_var->attrs, arg);
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
    long total_numval = 0;
    int err;

    if (DEBUG_CLASS_COMP_LEVEL_2) {
        knd_log(".. computing SUM of list: %.*s..",
                parent_var->name_size, parent_var->name);
    }

    for (curr_var = parent_var->list; curr_var; curr_var = curr_var->next) {
        if (DEBUG_CLASS_COMP_LEVEL_2) {
            knd_log("\n.. list elem: %.*s numval:%lu",
                    curr_var->id_size, curr_var->id, curr_var->numval);
            //str_attr_vars(curr_var->children, 1);
            //knd_log("QUERY:");
            //str_attr_vars(query, 1);
        }

        if (query->children) {
            if (!curr_var->children) continue;

            err = get_arg_value(curr_var->children, query->children, arg);
            if (err) return err;
            total_numval += arg->numval;
            continue;
        }

        if (curr_var->numval) {
            total_numval += curr_var->numval;
        }
    }

    *result = total_numval;
    return knd_OK;
}

static int compute_attr_var_value(struct kndClass *self  __attribute__((unused)),
                                  struct kndClassVar *src,
                                  struct kndAttrVar *query,
                                  struct kndAttr *attr,
                                  struct kndProcCallArg *arg)
{
    struct kndAttrVar *select_var;
    struct kndAttrVar *curr_var;
    struct kndAttrVar *field_var;
    int err;

    if (DEBUG_CLASS_COMP_LEVEL_1) {
        knd_log(".. query the \"%.*s\" class var..",
                src->entry->name_size, src->entry->name);
    }
    
    // TODO: compute sum of a list
    if (!memcmp("_sum", query->name, query->name_size)) {
        select_var = query->children;
        if (!select_var) return knd_FAIL;
        field_var = select_var->children;
        if (!field_var) return knd_FAIL;

        // find selected attr var
        for (curr_var = src->attrs; curr_var; curr_var = curr_var->next) {
            if (curr_var->name_size != select_var->name_size) continue;

            if (memcmp(curr_var->name,
                       select_var->name,
                       select_var->name_size)) continue; 

            err = compute_list_sum(curr_var, field_var, attr, arg, &arg->numval);
            if (err) return err;
        }
    }
    return knd_OK;
}

extern int knd_compute_class_attr_num_value(struct kndClass *self,
                                            struct kndClassVar *src_class_var,
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

    proc_call = &attr->proc->proc_call;

    if (DEBUG_CLASS_COMP_LEVEL_2) {
        knd_log("\nPROC CALL: \"%.*s\"",
                proc_call->name_size, proc_call->name);
        knd_log("== attr var: \"%.*s\"",
                attr_var->name_size, attr_var->name);
    }

    for (arg = proc_call->args; arg; arg = arg->next) {
        class_var = arg->class_var;
        if (DEBUG_CLASS_COMP_LEVEL_2)
            knd_log("ARG: %.*s", arg->name_size, arg->name);

        err = compute_attr_var_value(self,
                                     src_class_var,
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
        if (!divisor) {
            numval = (long)dividend;
            break;
        }

        //knd_log("\n.. divide %.2f by %.2f", dividend, divisor);
        result = (dividend / divisor) * (float)100;

        //knd_log("== result: %.2f", result);

        numval = (long)result;
        break;
    default:
        break;
    }
    
    attr_var->numval = numval;

    return knd_OK;
}



