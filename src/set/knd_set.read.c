#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "knd_class.h"
#include "knd_utils.h"
#include "knd_memblock.h"
#include "knd_mempool.h"
#include "knd_shared_set.h"
#include "knd_repo.h"
#include "knd_task.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#define DEBUG_SET_READ_LEVEL_0 0
#define DEBUG_SET_READ_LEVEL_1 0
#define DEBUG_SET_READ_LEVEL_2 0
#define DEBUG_SET_READ_LEVEL_3 0
#define DEBUG_SET_READ_LEVEL_4 0
#define DEBUG_SET_READ_LEVEL_TMP 1

int knd_set_leaf_open(struct kndSet *unused_var(s), struct kndStorageLeaf *leaf,
                      knd_set_elem_unmarshall_cb_t unused_var(cb),
                      struct kndTask *unused_var(task))
{
    knd_log("..open set {leaf %zu}", leaf->numid);

    return knd_OK;
}
