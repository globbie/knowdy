#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "knd_facet.h"
#include "knd_attr.h"
#include "knd_utils.h"
#include "knd_memblock.h"
#include "knd_mempool.h"
#include "knd_task.h"
#include "knd_utils.h"

#include <gsl-parser.h>

#define DEBUG_FACET_READ_LEVEL_0 0
#define DEBUG_FACET_READ_LEVEL_1 0
#define DEBUG_FACET_READ_LEVEL_2 0
#define DEBUG_FACET_READ_LEVEL_3 0
#define DEBUG_FACET_READ_LEVEL_4 0
#define DEBUG_FACET_READ_LEVEL_TMP 1

int knd_facet_read(struct kndFacet *facet, knd_attr_type attr_type, struct kndTask *unused_var(task))
{
    knd_log(".. reading {facet %zu {attr-type %d}}", facet->numid, attr_type);

    return knd_OK;
}

