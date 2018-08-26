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

#define DEBUG_GSL_LEVEL_1 0
#define DEBUG_GSL_LEVEL_2 0
#define DEBUG_GSL_LEVEL_3 0
#define DEBUG_GSL_LEVEL_4 0
#define DEBUG_GSL_LEVEL_5 0
#define DEBUG_GSL_LEVEL_TMP 1

extern int knd_class_export_updates_GSL(struct kndClass *self,
                                        struct kndUpdate *update,
                                        struct glbOutput *out)
{
    struct kndState *state;
    int err;

    err = out->writec(out, '{');                                                  RET_ERR();
    err = out->write(out, self->name, self->name_size);                           RET_ERR();
    err = out->write(out, "{id ", strlen("{id "));                                RET_ERR();
    err = out->write(out, self->entry->id, self->entry->id_size);                 RET_ERR();
    err = out->writec(out, '}');                                                  RET_ERR();
    
    if (self->states) {
        state = self->states;
        /*if (state->update == update) {
            err = knd_class_export_GSL(c, out);                          RET_ERR();
            }*/
    }
    
    err = out->writec(out, '}');                                          RET_ERR();
    return knd_OK;
}
