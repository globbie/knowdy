/**
 *   Copyright (c) 2011-present by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the Knowdy Graph DB, 
 *   and as such it is subject to the license stated
 *   in the LICENSE file which you have received 
 *   as part of this distribution.
 *
 *   Project homepage:
 *   <http://www.knowdy.net>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   -----------
 *   knd_config.h
 *   Knowdy configuration settings
 */

#pragma once

#include <stdlib.h>
#include <stddef.h>

#include "knd_err.h"

/* comparison codes */
typedef enum { knd_EQUALS, knd_LESS, knd_MORE, knd_NOT_COMPARABLE } knd_comparison_codes;

typedef enum knd_format { KND_FORMAT_GSL,
                          KND_FORMAT_JSON,
                          KND_FORMAT_HTML,
                          KND_FORMAT_SVG,
                          KND_FORMAT_GSP
                         } knd_format;

static const char *const knd_format_names[] = {
    [KND_FORMAT_GSL] = "GSL",
    [KND_FORMAT_JSON] = "JSON",
    [KND_FORMAT_HTML] = "HTML",
    [KND_FORMAT_SVG] = "SVG",
    [KND_FORMAT_GSP] = "GSP"
};

typedef enum knd_logic { KND_LOGIC_AND, 
                          KND_LOGIC_OR,
                          KND_LOGIC_NOT
} knd_logic;

#define RET_ERR(S)  if (err) { printf("%s", "" #S);                               \
                              printf ("-- <%s> failed at line %d of file \"%s\"\n",\
                                      __func__, __LINE__, __FILE__); return err; } 
#define MEMPOOL_ERR(S) if (err) { printf("-- mempool failed to alloc \"%s\"\n", "" #S);                               \
        printf ("-- <%s> failed at line %d of file \"%s\"\n",           \
                __func__, __LINE__, __FILE__); return err; } 
#define ALLOC_ERR(V) if (!(V)) { return knd_NOMEM; }
#define PARSE_ERR(V) if (err) { printf("LINEAR POS:%zu", *total_size); return err; } 

#define KND_TASK_ERR(...) \
    if (err) { \
        task->out->reset(task->out);\
        int e = task->out->writef(task->out, "" __VA_ARGS__); \
        if (e) return e; \
        if (task->log->buf_size != 0) { \
            e = task->out->write(task->out,      \
                      " <= ", strlen(" <= "));  \
            if (e) return e; \
            e = task->out->write(task->out, task->log->buf, task->log->buf_size); \
            if (e) return e; \
        }\
        task->log->reset(task->log); \
        e = task->log->write(task->log, task->out->buf, task->out->buf_size); \
        if (e) return e; \
        task->output = task->log->buf; \
        task->output_size = task->log->buf_size; \
        return err;\
    }

#define KND_TASK_LOG(...)                     \
    do {                                     \
        task->out->reset(task->out);           \
        int e = task->out->writef(task->out,   \
          "" __VA_ARGS__);                   \
        if (e) break;                        \
        if (task->log->buf_size != 0) {       \
          e = task->out->write(task->out,      \
                   " <= ", strlen(" <= "));  \
          if (e) break;                      \
          e = task->out->write(task->out,      \
          task->log->buf, task->log->buf_size);\
          if (e) break;                      \
        }                                    \
        task->log->reset(task->log);           \
        e = task->log->write(task->log,        \
         task->out->buf, task->out->buf_size); \
        if (e) break;                        \
        task->output = task->log->buf;         \
        task->output_size = task->log->buf_size;\
    } while (0)


#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L

#include <stdbool.h>

#else

#undef bool
#undef true
#undef false

#define bool unsigned char
#define true  1
#define false 0

#endif

#define unused_var(x) unused_var_ ##x __attribute__((unused))

#define KND_OFFSET_SIZE 4
#define KND_UINT_SIZE 4

#define KND_IDLE_TIMEOUT 10 /* in seconds */

#define KND_TMP_DIR "/tmp"

/* debugging output levels */
#define KND_DEBUG_LEVEL_1 1
#define KND_DEBUG_LEVEL_2 1
#define KND_DEBUG_LEVEL_3 0
#define KND_DEBUG_LEVEL_4 0
#define KND_DEBUG_LEVEL_5 0

#define MAX_DEQUE_ATTEMPTS 100
#define TASK_TIMEOUT_USECS 100
#define TASK_MAX_TIMEOUT_SECS 5
#define TASK_QUEUE_CAPACITY 20

#define KND_MAX_JOURNALS 64
#define KND_MAX_JOURNAL_SIZE 1024 * 1024
#define KND_MAX_SNAPSHOTS 256

#define KND_RESULT_BATCH_SIZE 10
#define KND_RESULT_MAX_BATCH_SIZE 500

#define KND_MAX_DEBUG_CONTEXT_SIZE 2048

#define KND_ID_SIZE  (8 * sizeof(char))
#define KND_ID_BATCH_SIZE 10
#define KND_LOCALE_SIZE 8

#define KND_MAX_MIGRATIONS 256
#define KND_MAX_SPECS 8

#define KND_MAX_TASKS 64

#define KND_MAX_BASES 256
#define KND_MAX_INHERITED 256
#define KND_MAX_RELS 1024
#define KND_MAX_PROCS 1024

#define KND_DIR_SIZE_ENCODE_BASE 10
#define KND_DIR_ENTRY_SIZE 16
#define KND_MAX_ARGS 16
#define KND_DIR_TRAILER_MAX_SIZE 1024 * 1024 * 10

#define KND_SID_SIZE 128
#define KND_TID_SIZE 128

#define KND_MAX_TIDS 1024
#define KND_MAX_USERS 1024 * 1024

#define KND_MAX_GEOIPS 160000
#define KND_MAX_GEO_LOCS 3200

/* KND Object */
#define KND_OBJ_METABUF_SIZE 1024

#define KND_MAX_DEPTH 256
#define KND_DEFAULT_CLASS_DEPTH 3
#define KND_MAX_CLASS_DEPTH 3
#define KND_MAX_CLASS_BATCH 128

#define KND_DEFAULT_OBJ_DEPTH 2
#define KND_MAX_OBJ_DEPTH 4

#define KND_MAX_FLAT_ROWS 256
#define KND_MAX_FLAT_COLS 64


#define KND_NUMFIELD_MAX_SIZE 8

#define KND_LOC_SEPAR "/"
#define KND_FACET_SEPAR "#"
#define KND_TEXT_CHUNK_SEPAR " "


#define KND_GROW_FACTOR 2

#define KND_NAME_LENGTH 50

/* default numeric base */
#define KND_NUM_ENCODE_BASE 10

#define KND_CONC_PREFIX_NUM_DIGITS 3
#define KND_CONC_PREFIX_DIGIT_SIZE 2
#define KND_CONC_PREFIX_SIZE  KND_CONC_PREFIX_NUM_DIGITS *  KND_CONC_PREFIX_DIGIT_SIZE

#define KND_TREE_OFFSET_SIZE 2

#define KND_INDEX_ID_BATCH_SIZE 300

#define SPACE_CHAR 32

#define KND_LOCSET_MAX_ENUM_VALUE 64

#define KND_MAX_ATTRS 264
#define KND_MAX_COMPUTED_ATTRS 16
#define KND_MAX_CLAUSES 32
#define KND_MAX_ELEMLOCS 128

#define KND_LEAF_SIZE 10

/* number of leafs to read from index at once */
#define KND_LEAF_CHUNK_SIZE 100

#define KND_SIZE_OF_OFFSET sizeof(size_t)

/* alphanumeric symbols:
   0-9, A-Z, a-z */
#define KND_RADIX_BASE 62

#define UCHAR_NUMVAL_RANGE (unsigned char)-1 + 1

/* hash table sizes */
#define KND_HUGE_DICT_SIZE 100000
#define KND_LARGE_DICT_SIZE 10000
#define KND_MEDIUM_DICT_SIZE 1000
#define KND_SMALL_DICT_SIZE 100
#define KND_TINY_DICT_SIZE 10

#define KND_OBJ_STORAGE_SIZE 100
#define KND_TRN_STORAGE_SIZE 10000

#define KND_UPDATE_OPER_STORAGE_SIZE 8

#define KND_CONC_STORAGE_SIZE 1024 * 20
#define KND_CONC_STORAGE_MIN 1024
#define KND_CONC_STORAGE_MAX 1024 * 1024


#define KND_POLICY_STORAGE_SIZE 16

#define KND_CONC_SPEC_STORAGE_SIZE 1024 * 1024
#define KND_CONC_TOPIC_STORAGE_SIZE 1024 * 1024
#define KND_CONC_ROLE_STORAGE_SIZE 1024 * 1024
#define KND_CONC_REF_STORAGE_SIZE 1024 * 1024

#define KND_MAZE_NUM_AGENTS 100
#define KND_MAZE_NUM_CACHE_SETS 1024

#define KND_SET_MAX_DEPTH 10
#define KND_TEMP_BUF_SIZE 1024
#define KND_MED_BUF_SIZE 1024 * 5

#define KND_FILE_BUF_SIZE 1024 * 1024 * 10
#define KND_LARGE_BUF_SIZE 1024 * 1024
#define KND_SMALL_BUF_SIZE 64
#define KND_LABEL_SIZE 8
#define KND_PATH_SIZE 1024

#define KND_NAME_SIZE 900
#define KND_SHORT_NAME_SIZE 48
#define KND_VAL_SIZE 1024 * 2

#define KND_UID_SIZE 7

#define KND_CONC_NAME_BUF_SIZE 1024

#define KND_TASK_STORAGE_SIZE 100 * 1024 * 1024 * sizeof(char)

#define KND_SEARCH_BUF_SIZE 1024 * 100 * sizeof(char)
#define KND_IDX_BUF_SIZE 10 * 1024 * 1024 * sizeof(char)


#define KND_LARGE_MEMPAGE_SIZE 4096 * 4
#define KND_NUM_LARGE_MEMPAGES 1000

#define KND_BASE_X4_MEMPAGE_SIZE 4096
#define KND_NUM_BASE_X4_MEMPAGES 10000

#define KND_BASE_X2_MEMPAGE_SIZE 2048
#define KND_BASE_X2_MEMPAGES 10000

#define KND_BASE_MEMPAGE_SIZE 1024
#define KND_NUM_BASE_MEMPAGES 10000

#define KND_SMALL_X4_MEMPAGE_SIZE 512
#define KND_NUM_SMALL_X4_MEMPAGES 10000

#define KND_SMALL_X2_MEMPAGE_SIZE 256
#define KND_NUM_SMALL_X2_MEMPAGES 10000

#define KND_SMALL_MEMPAGE_SIZE 128
#define KND_NUM_SMALL_MEMPAGES 10000

#define KND_TINY_MEMPAGE_SIZE 64
#define KND_NUM_TINY_MEMPAGES 10000

#define KND_MIN_USER_CONTEXTS 1024
#define KND_MIN_UPDATES 1024
#define KND_MIN_STATES 1024 * 10
#define KND_MIN_USERS 1024
#define KND_MIN_CLASSES 1024
#define KND_MIN_ATTRS  1024 * 10
#define KND_MIN_OBJS 1024 * 10
#define KND_MIN_OBJ_ENTRIES 1024
#define KND_MIN_ELEMS 1024
#define KND_MIN_RELS 1024
#define KND_MIN_REL_INSTANCES 1024
#define KND_MIN_RELARG_INSTANCES 1024 * 4
#define KND_MIN_PROCS 1024
#define KND_MIN_PROC_INSTANCES 1024

#define KND_TEXT_CHUNK_SIZE 128
#define KND_MAX_TEXT_CHUNK_SIZE 1024 * 10
#define KND_MAX_DEBUG_CHUNK_SIZE 512

#define KND_MAX_CONTEXTS 4

#define KND_SETS_BATCH_SIZE 32
#define KND_MAX_SETS 128

#define KND_SET_INBOX_SIZE 8
#define KND_SET_MIN_SIZE KND_MAX_INBOX_SIZE / 10
#define KND_INBOX_RESERVE_RATIO 1.4

#define KND_FEATURED_SIZE 5
#define KND_FEATURED_MAX_SIZE 10

#define KND_SIMILAR_BATCH_SIZE 5
#define KND_SIMILAR_MAX_SIZE 30

#define KND_SPECS_BATCH_SIZE 5
#define KND_SPECS_MAX_SIZE 30

#define KND_MAX_RESULT_SIZE (1024 * sizeof(char))
#define KND_CONTEXT_RADIUS 100

