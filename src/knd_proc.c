#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>

#include <gsl-parser.h>
#include <glb-lib/output.h>

#include "knd_proc.h"
#include "knd_proc_arg.h"
#include "knd_class.h"
#include "knd_task.h"
#include "knd_state.h"
#include "knd_mempool.h"
#include "knd_utils.h"
#include "knd_text.h"
#include "knd_dict.h"
#include "knd_repo.h"

#define DEBUG_PROC_LEVEL_0 0
#define DEBUG_PROC_LEVEL_1 0
#define DEBUG_PROC_LEVEL_2 0
#define DEBUG_PROC_LEVEL_3 0
#define DEBUG_PROC_LEVEL_TMP 1

struct LocalContext {
    struct kndRepo *repo;
    struct kndTask *task;
    struct kndProc *proc;
};

static int resolve_proc_call(struct kndProc *self);

static int inherit_args(struct kndProc *self,
                        struct kndProc *parent,
                        struct kndTask *task);

static void proc_call_arg_str(struct kndProcCallArg *self,
                              size_t depth)
{
    const char *arg_type = "";
    size_t arg_type_size = 0;

    if (self->arg) {
        arg_type = self->arg->classname;
        arg_type_size = self->arg->classname_size;
    }

    knd_log("%*s  {%.*s %.*s [class:%.*s]}", depth * KND_OFFSET_SIZE, "",
            self->name_size, self->name,
            self->val_size, self->val, arg_type_size, arg_type);
}

static void str(struct kndProc *self)
{
    struct kndTranslation *tr;
    struct kndProcArg *arg;
    struct kndProcCallArg *call_arg;
    size_t depth = 0;

    knd_log("{proc %.*s  {_id %.*s}",
            self->name_size, self->name,
            self->entry->id_size, self->entry->id);

    for (tr = self->tr; tr; tr = tr->next) {
        knd_log("%*s~ %.*s %.*s", (depth + 1) * KND_OFFSET_SIZE, "",
                tr->locale_size, tr->locale, tr->val_size, tr->val);
    }

    /*for (base = self->bases; base; base = base->next) {
        base_str(base, depth + 1);
        }*/

    if (self->result_classname_size) {
        knd_log("%*s    {result class:%.*s}", depth * KND_OFFSET_SIZE, "",
                self->result_classname_size, self->result_classname);
    }

    for (arg = self->args; arg; arg = arg->next) {
        knd_proc_arg_str(arg, depth + 1);
    }

    if (self->proc_call) {
        knd_log("%*s    {do %.*s", depth * KND_OFFSET_SIZE, "",
                self->proc_call->name_size, self->proc_call->name);
        for (call_arg = self->proc_call->args; call_arg; call_arg = call_arg->next) {
            proc_call_arg_str(call_arg, depth + 1);
        }
        knd_log("%*s    }", depth * KND_OFFSET_SIZE, "");
    }
    knd_log("}");
}

extern int knd_get_proc(struct kndRepo *repo,
                        const char *name, size_t name_size,
                        struct kndProc **result)
{
    struct kndProcEntry *entry;
    struct kndProc *proc;
    int err;

    if (DEBUG_PROC_LEVEL_TMP)
        knd_log(".. repo %.*s to get proc: \"%.*s\"..",
                repo->name_size, repo->name, name_size, name);

    entry = (struct kndProcEntry*)repo->proc_name_idx->get(repo->proc_name_idx,
                                                           name, name_size);
    if (!entry) {
        if (repo->base) {
            err = knd_get_proc(repo->base, name, name_size, result);
            if (err) return err;
            return knd_OK;
        }

        knd_log("-- no such proc: \"%.*s\"", name_size, name);

        /*repo->log->reset(repo->log);
        err = repo->log->write(repo->log, name, name_size);
        if (err) return err;
        err = repo->log->write(repo->log, " Proc name not found",
                               strlen(" Proc name not found"));
                               if (err) return err;*/
        return knd_NO_MATCH;
    }

    if (entry->phase == KND_REMOVED) {
        knd_log("-- \"%s\" proc was removed", name);
        /*repo->log->reset(repo->log);
        err = repo->log->write(repo->log, name, name_size);
        if (err) return err;
        err = repo->log->write(repo->log, " proc was removed",
                               strlen(" proc was removed"));
        if (err) return err;
        */
        //repo->root_proc->task->http_code = HTTP_GONE;
        return knd_NO_MATCH;
    }

    if (entry->proc) {
        proc = entry->proc;
        entry->phase = KND_SELECTED;
        *result = proc;
        return knd_OK;
    }

    // TODO: defreeze
    return knd_FAIL;
}

static gsl_err_t run_get_proc(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndRepo *repo = ctx->repo;
    struct kndProc *proc;
    int err;

    if (!name_size) return make_gsl_err(gsl_FORMAT);
    if (name_size >= KND_NAME_SIZE) return make_gsl_err(gsl_LIMIT);

    ctx->proc = NULL;

    err = knd_get_proc(repo, name, name_size, &proc);
    if (err) return make_gsl_err_external(err);
    ctx->proc = proc;

    return make_gsl_err(gsl_OK);
}

static gsl_err_t present_proc_selection(void *obj,
                                        const char *unused_var(val),
                                        size_t unused_var(val_size))
{
    struct LocalContext *ctx = obj;
    struct kndTask *task = ctx->task;
    struct kndProc *proc = ctx->proc;
    knd_format format = task->format;
    struct glbOutput *out = task->out;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. presenting proc selection..");

    if (!proc) return make_gsl_err(gsl_FAIL);

    out->reset(out);

    /* export BODY */
    err = knd_proc_export(proc, format, task, out);
    if (err) return make_gsl_err_external(err);

    return make_gsl_err(gsl_OK);
}

static gsl_err_t remove_proc(void *obj, const char *name, size_t name_size)
{
    struct LocalContext *ctx = obj;
    struct kndProc *proc = ctx->proc;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. removing proc: %.*s", name_size, name);

    if (!proc) {
        knd_log("-- remove operation: no proc selected");

        /*repo->log->reset(repo->log);
        err = repo->log->write(repo->log, name, name_size);
        if (err) return make_gsl_err_external(err);
        err = repo->log->write(repo->log, " class name not specified",
                               strlen(" class name not specified"));
                               if (err) return make_gsl_err_external(err);*/
        return make_gsl_err(gsl_NO_MATCH);
    }

    if (DEBUG_PROC_LEVEL_2)
        knd_log("== proc to remove: \"%.*s\"\n",
                proc->name_size, proc->name);

    proc->entry->phase = KND_REMOVED;

    //repo->log->reset(repo->log);
    /*err = repo->log->write(repo->log, proc->name, proc->name_size);
    if (err) return make_gsl_err_external(err);
    err = repo->log->write(repo->log, " proc removed",
                           strlen(" proc removed"));
    if (err) return make_gsl_err_external(err);
    */
    /*    proc->next = self->inbox;
    self->inbox = proc;
    self->inbox_size++;
    */

    return make_gsl_err(gsl_OK);
}

extern gsl_err_t knd_proc_select(struct kndRepo *repo,
                                 const char *rec,
                                 size_t *total_size,
                                 struct kndTask *task)
{
    struct LocalContext ctx = {
        .task = task,
        .repo = repo
    };
    gsl_err_t parser_err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. parsing Proc select: \"%.*s\"",
                16, rec);

    struct gslTaskSpec specs[] = {
        { .is_implied = true,
          .is_selector = true,
          .run = run_get_proc,
          .obj = &ctx
        },
        { .type = GSL_SET_STATE,
          .name = "_rm",
          .name_size = strlen("_rm"),
          .run = remove_proc,
          .obj = &ctx
        }/*,
        { .type = GSL_SET_STATE,
          .name = "inst",
          .name_size = strlen("inst"),
          .parse = parse_import_instance,
          .obj = self
          }*/,
        { .is_default = true,
          .run = present_proc_selection,
          .obj = &ctx
        }
    };

    parser_err = gsl_parse_task(rec, total_size, specs,
                                sizeof specs / sizeof specs[0]);
    if (parser_err.code) {
        /*knd_log("-- proc parse error: \"%.*s\"",
                repo->log->buf_size, repo->log->buf);
        if (!repo->log->buf_size) {
            e = repo->log->write(repo->log, "proc parse failure",
                                 strlen("proc parse failure"));
            if (e) return make_gsl_err_external(e);
            }*/
        return parser_err;
    }

    /* any updates happened? */
    /*if (self->curr_proc) {
        if (self->curr_proc->inbox_size || self->curr_proc->inst_inbox_size) {
            self->curr_proc->next = self->inbox;
            self->inbox = self->curr_proc;
            self->inbox_size++;
        }
        }*/

    return make_gsl_err(gsl_OK);
}

int knd_proc_export(struct kndProc *self,
                    knd_format format,
                    struct kndTask *task,
                    struct glbOutput *out)
{
    int err;

    switch (format) {
    case KND_FORMAT_JSON:
        err = knd_proc_export_JSON(self, task, out);
        if (err) return err;
        break;
        /*case KND_FORMAT_GSP:
        err = knd_proc_export_GSP(self, task, out);
        if (err) return err;
        break;*/
    case KND_FORMAT_SVG:
        err = knd_proc_export_SVG(self, task, out);
        if (err) return err;
        break;
    default:
        break;
    }
    
    return knd_OK;
}

static int resolve_parents(struct kndProc *self,
                           struct kndTask *task)
{
    struct kndProcVar *base;
    struct kndProc *proc;
    struct kndProcArg *arg;
    struct kndProcArgEntry *arg_entry;
    struct kndProcArgVar *arg_item;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. resolve parent procs of \"%.*s\"..",
                self->name_size, self->name);

    /* resolve refs  */
    for (base = self->bases; base; base = base->next) {
        if (DEBUG_PROC_LEVEL_2)
            knd_log("\n.. \"%.*s\" proc to get its parent: \"%.*s\"..",
                    self->name_size, self->name,
                    base->name_size, base->name);

        err = knd_get_proc(self->entry->repo,
                           base->name, base->name_size, &proc);                 RET_ERR();
        if (proc == self) {
            knd_log("-- self reference detected in \"%.*s\" :(",
                    base->name_size, base->name);
            return knd_FAIL;
        }

        base->proc = proc;

        /* should we keep track of our children? */
        /*if (c->ignore_children) continue; */

        /* check base doublets */
        /*    for (size_t i = 0; i < self->num_children; i++) {
            entry = self->children[i];
            if (entry->proc == self) {
                knd_log("-- doublet proc found in \"%.*s\" :(",
                        self->name_size, self->name);
                return knd_FAIL;
            }
            }*/

        /*if (proc->num_children >= KND_MAX_PROC_CHILDREN) {
            knd_log("-- %s as child to %s - max proc children exceeded :(",
                    self->name, base->name);
            return knd_FAIL;
        }
        entry = &proc->children[proc->num_children];
        entry->proc = self;
        proc->num_children++;
        */
        if (DEBUG_PROC_LEVEL_2)
            knd_log("\n\n.. children of proc \"%.*s\": %zu",
                    proc->name_size, proc->name, proc->num_children);
        
        err = inherit_args(self, base->proc, task);                                     RET_ERR();

        /* redefine inherited args if needed */

        for (arg_item = base->args; arg_item; arg_item = arg_item->next) {
            arg_entry = self->arg_idx->get(self->arg_idx,
                                       arg_item->name, arg_item->name_size);
            if (!arg_entry) {
                knd_log("-- no arg \"%.*s\" in proc \"%.*s\' :(",
                        arg_item->name_size, arg_item->name,
                        proc->name_size, proc->name);
                return knd_FAIL;
            }

            /* TODO: check class inheritance */

            if (DEBUG_PROC_LEVEL_2)
                knd_log(".. arg \"%.*s\" [class:%.*s] to replace \"%.*s\" [class:%.*s]",
                        arg_item->name_size, arg_item->name,
                        arg_item->classname_size, arg_item->classname,
                        arg_entry->arg->name_size, arg_entry->arg->name,
                        arg_entry->arg->classname_size, arg_entry->arg->classname);

            err = knd_proc_arg_new(&arg, task->mempool);
            if (err) return err;

            arg->name = arg_item->name;
            arg->name_size = arg_item->name_size;

            arg->classname = arg_item->classname;
            arg->classname_size = arg_item->classname_size;
            arg_entry->arg = arg;

            arg->parent = self;
            arg->next = self->args;
            self->args = arg;
            self->num_args++;
        }
    }

    return knd_OK;
}

static int proc_resolve(struct kndProc *self,
                        struct kndTask *task)
{
    struct kndProcArg *arg = NULL;
    //struct kndProcArgEntry *arg_entry;
    //struct kndProcEntry *entry;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. resolving PROC: %.*s",
                self->name_size, self->name);

    if (!self->arg_idx) {
        err = ooDict_new(&self->arg_idx, KND_SMALL_DICT_SIZE);                    RET_ERR();

        for (arg = self->args; arg; arg = arg->next) {
            err = knd_proc_resolve_arg(arg, task->repo);                          RET_ERR();

            /* TODO: index  arg entry */
            /*arg_entry = malloc(sizeof(struct kndProcArgEntry));
            if (!arg_entry) return knd_NOMEM;

            memset(arg_entry, 0, sizeof(struct kndProcArgEntry));

            arg_entry->name = arg->name;
            arg_entry->name_size = arg->name_size;
            arg_entry->arg = arg;
            err = self->arg_idx->set(self->arg_idx,
                                     arg_entry->name,
                                     arg_entry->name_size, (void*)arg_entry);
            if (err) return err;
            */

            /*if (arg->proc_entry) {
                entry = arg->proc_entry;
                if (entry->proc) {
                    if (DEBUG_PROC_LEVEL_2)
                        knd_log("== ARG proc estimate: %zu", entry->proc->estim_cost_total);
                    self->estim_cost_total += entry->proc->estim_cost_total;
                }
                } */
        }
    }

    if (self->bases) {
        err = resolve_parents(self, task);                                              RET_ERR();
    }

    if (self->proc_call) {
        err = resolve_proc_call(self);                                                  RET_ERR();
    }
    
    self->is_resolved = true;

    return knd_OK;
}

static int inherit_args(struct kndProc *self,
                        struct kndProc *parent,
                        struct kndTask *task)
{
    struct kndProcArg *arg;
    struct kndProcArgEntry *arg_entry;
    struct kndProcVar *base;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. \"%.*s\" proc to inherit args from \"%.*s\" (num args:%zu)",
                self->name_size, self->name, parent->name_size, parent->name, parent->num_args);

    if (!parent->is_resolved) {
        err = proc_resolve(parent, task);                                            RET_ERR();
    }

    /* check circled relations */
    /*    for (size_t i = 0; i < self->num_inherited; i++) {
        entry = self->inherited[i];
        proc = entry->proc;

        if (DEBUG_PROC_LEVEL_2)
            knd_log("== (%zu of %zu)  \"%.*s\" is a parent of \"%.*s\"", 
                    i, self->num_inherited, proc->name_size, proc->name,
                    self->name_size, self->name);
        if (entry->proc == parent) {
            knd_log("-- circle inheritance detected for \"%.*s\" :(",
                    parent->name_size, parent->name);
            return knd_FAIL;
        }
    }
    */
    /* get args from parent */
    for (arg = parent->args; arg; arg = arg->next) {

        /* compare with exiting args */
        arg_entry = self->arg_idx->get(self->arg_idx,
                                   arg->name, arg->name_size);
        if (arg_entry) {
            knd_log("-- arg \"%.*s\" collision between \"%.*s\""
                    " and parent proc \"%.*s\"?",
                    arg_entry->name_size, arg_entry->name,
                    self->name_size, self->name,
                    parent->name_size, parent->name);
            return knd_OK;
        }

        /* register arg entry */
        arg_entry = malloc(sizeof(struct kndProcArgEntry));
        if (!arg_entry) return knd_NOMEM;

        memset(arg_entry, 0, sizeof(struct kndProcArgEntry));

        arg_entry->name = arg->name;
        arg_entry->name_size = arg->name_size;
        arg_entry->arg = arg;

        if (DEBUG_PROC_LEVEL_2)
            knd_log("NB: ++ proc \"%.*s\" inherits arg \"%.*s\" from \"%.*s\"",
                    self->name_size, self->name,
                    arg->name_size, arg->name,
                    parent->name_size, parent->name);

        err = self->arg_idx->set(self->arg_idx,
                                 arg_entry->name, arg_entry->name_size,
                                 (void*)arg_entry);
        if (err) return err;
    }
    
    if (self->num_inherited >= KND_MAX_INHERITED) {
        knd_log("-- max inherited exceeded for %.*s :(",
                self->name_size, self->name);
        return knd_FAIL;
    }

    if (DEBUG_PROC_LEVEL_2)
        knd_log(" .. add \"%.*s\" parent to \"%.*s\"",
                parent->entry->proc->name_size,
                parent->entry->proc->name,
                self->name_size, self->name);

    //    self->inherited[self->num_inherited] = parent->entry;
    //self->num_inherited++;

    /* contact the grandparents */
    for (base = parent->bases; base; base = base->next) {
        if (base->proc) {
            err = inherit_args(self, base->proc, task);                                 RET_ERR();
        }
    }
    
    return knd_OK;
}


static int resolve_proc_call(struct kndProc *self)
{
    struct kndProcCallArg *call_arg;
    struct kndProcArgEntry *entry;

    if (DEBUG_PROC_LEVEL_TMP)
        knd_log(".. resolving proc call %.*s ..",
                self->proc_call->name_size, self->proc_call->name);

    if (!self->arg_idx) return knd_FAIL;

    for (call_arg = self->proc_call->args; call_arg; call_arg = call_arg->next) {
        if (DEBUG_PROC_LEVEL_2)
            knd_log(".. proc call arg %.*s ..",
                    call_arg->name_size, call_arg->name,
                    call_arg->val_size, call_arg->val);

        entry = self->arg_idx->get(self->arg_idx,
                                   call_arg->val, call_arg->val_size);
        if (!entry) {
            knd_log("-- couldn't resolve proc call arg %.*s: %.*s :(",
                call_arg->name_size, call_arg->name,
                call_arg->val_size, call_arg->val);
            return knd_FAIL;
        }

        call_arg->arg = entry->arg;
    }

    return knd_OK;
}


static int resolve_procs(struct kndProc *self,
                         struct kndTask *task)
{
    struct kndProc *proc;
    struct kndRepo *repo = self->entry->repo;
    struct kndProcEntry *entry;
    const char *key;
    void *val;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. resolving procs by \"%.*s\"",
                self->name_size, self->name);
    key = NULL;
    repo->proc_name_idx->rewind(repo->proc_name_idx);
    do {
        repo->proc_name_idx->next_item(repo->proc_name_idx, &key, &val);
        if (!key) break;

        entry = (struct kndProcEntry*)val;
        proc = entry->proc;

        if (proc->is_resolved) {
            /*knd_log("--");
              proc->str(proc); */
            continue;
        }

        err = proc_resolve(proc, task);
        if (err) {
            knd_log("-- couldn't resolve the \"%s\" proc :(", proc->name);
            return err;
        }

        if (DEBUG_PROC_LEVEL_2) {
            knd_log("--");
            proc->str(proc);
        }
    } while (key);

    return knd_OK;
}

extern int knd_proc_coordinate(struct kndProc *self,
                               struct kndTask *task)
{
    struct kndProc *proc;
    struct kndRepo *repo = self->entry->repo;
    struct kndProcEntry *entry;
    const char *key;
    void *val;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. proc coordination in progress ..");

    err = resolve_procs(self, task);                                                   RET_ERR();

    /* assign ids */
    key = NULL;
    repo->proc_name_idx->rewind(repo->proc_name_idx);
    do {
        repo->proc_name_idx->next_item(repo->proc_name_idx, &key, &val);
        if (!key) break;

        entry = (struct kndProcEntry*)val;
        proc = entry->proc;

        /* assign id */
        self->next_id++;
        proc->id = self->next_id;
        proc->entry->phase = KND_CREATED;
    } while (key);

    /* display all procs */
    if (DEBUG_PROC_LEVEL_2) {
        key = NULL;
        repo->proc_name_idx->rewind(repo->proc_name_idx);
        do {
            repo->proc_name_idx->next_item(repo->proc_name_idx, &key, &val);
            if (!key) break;
            entry = (struct kndProcEntry*)val;
            proc = entry->proc;
            proc->str(proc);
        } while (key);
    }

    return knd_OK;
}

extern int knd_resolve_proc_ref(struct kndClass *self,
                                const char *name, size_t name_size,
                                struct kndProc *unused_var(base),
                                struct kndProc **result,
                                struct kndTask *unused_var(task))
{
    struct kndProc *proc;
    int err;

    if (DEBUG_PROC_LEVEL_2)
        knd_log(".. resolving proc ref:  %.*s", name_size, name);

    err = knd_get_proc(self->entry->repo,
                       name, name_size, &proc);                            RET_ERR();

    /*c = dir->conc;
    if (!c->is_resolved) {
        err = knd_class_resolve(c);                                                RET_ERR();
    }

    if (base) {
        err = is_base(base, c);                                                   RET_ERR();
    }
    */

    *result = proc;

    return knd_OK;
}


extern void knd_proc_init(struct kndProc *self)
{
    self->str = str;
}

extern int knd_proc_call_arg_new(struct kndMemPool *mempool,
                                 struct kndProcCallArg **result)
{
    void *page;
    int err;
    //knd_log("..proc call arg new [size:%zu]", sizeof(struct kndProcCallArg));
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndProcCallArg), &page);  RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_proc_call_new(struct kndMemPool *mempool,
                             struct kndProcCall **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndProcCall), &page);  RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_proc_var_new(struct kndMemPool *mempool,
                            struct kndProcVar **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndProcVar), &page);          RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_proc_arg_var_new(struct kndMemPool *mempool,
                                struct kndProcArgVar **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_TINY,
                            sizeof(struct kndProcArgVar), &page);          RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_proc_entry_new(struct kndMemPool *mempool,
                              struct kndProcEntry **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL,
                            sizeof(struct kndProcEntry), &page);                  RET_ERR();
    *result = page;
    return knd_OK;
}

extern int knd_proc_new(struct kndMemPool *mempool,
                        struct kndProc **result)
{
    void *page;
    int err;
    err = knd_mempool_alloc(mempool, KND_MEMPAGE_SMALL_X2,
                            sizeof(struct kndProc), &page);                       RET_ERR();
    *result = page;
    knd_proc_init(*result);
    return knd_OK;
}
