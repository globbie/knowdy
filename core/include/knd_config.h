/**
 *   Copyright (c) 2011-2017 by Dmitri Dmitriev
 *   All rights reserved.
 *
 *   This file is part of the Knowdy Search Engine, 
 *   and as such it is subject to the license stated
 *   in the LICENSE file which you have received 
 *   as part of this distribution.
 *
 *   Project homepage:
 *   <http://www.globbie.net>
 *
 *   Initial author and maintainer:
 *         Dmitri Dmitriev aka M0nsteR <dmitri@globbie.net>
 *
 *   -----------
 *   knd_config.h
 *   Knowdy configuration settings
 */

#ifndef KND_CONFIG_H
#define KND_CONFIG_H

/* return error codes */
typedef enum { knd_OK, knd_FAIL, knd_NOMEM, knd_LIMIT, knd_AUTH_OK, knd_AUTH_FAIL,
        knd_INVALID_DATA, knd_ACCESS, knd_NO_MATCH, knd_MATCH_FOUND, knd_FORMAT,
        knd_IO_FAIL, knd_EOB, knd_STOP, knd_NEED_WAIT, 
        knd_EXPIRED, knd_MAX_LIMIT_REACHED,
        KND_OPEN_DELIM_MISSING, KND_CLOSE_DELIM_MISSING } 
  knd_err_codes;

typedef enum knd_state_phase { KND_SELECTED,
                               KND_SUBMITTED,
                               KND_CREATED,
                               KND_UPDATED,
                               KND_REMOVED,
                               KND_FREED,
                               KND_FROZEN,
                               KND_RESTORED } knd_state_phase;

/* comparison codes */
typedef enum { knd_EQUALS, knd_LESS, knd_MORE, knd_NOT_COMPARABLE } knd_comparison_codes;

typedef enum knd_format { KND_FORMAT_JSON, 
                          KND_FORMAT_XML,
                          KND_FORMAT_HTML,
                          KND_FORMAT_JS,
                          KND_FORMAT_GSL,
                          KND_FORMAT_GSP,
                          KND_FORMAT_GSC
                         } knd_format;

static const char* const knd_format_names[] = {
    "JSON",
    "XML",
    "HTML", 
    "JS",
    "GSL", 
    "GSP", 
    "GSC" };

typedef enum knd_logic { KND_LOGIC_AND, 
                          KND_LOGIC_OR,
                          KND_LOGIC_NOT
} knd_logic;

typedef enum knd_agent_role { KND_WRITER, 
                              KND_READER
} knd_agent_role;

typedef enum knd_storage_type {
    KND_STORAGE_DB, 
    KND_STORAGE_XML
} knd_storage_type;



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

#define LOGIC_NOT 0
#define LOGIC_AND 1 
#define LOGIC_OR  2

#define KND_OFFSET_SIZE 4

#define KND_IDLE_TIMEOUT 10 /* in seconds */

#define GENERIC_TOPIC_NAME_RU "Общее"
#define KND_NUM_RATINGS 8

#define KND_TMP_DIR "/tmp"

/* debugging output levels */
#define KND_DEBUG_LEVEL_1 1
#define KND_DEBUG_LEVEL_2 1
#define KND_DEBUG_LEVEL_3 0
#define KND_DEBUG_LEVEL_4 0
#define KND_DEBUG_LEVEL_5 0

#define KND_RESULT_BATCH_SIZE 10
#define KND_RESULT_MAX_BATCH_SIZE 100

#define KND_ID_MATRIX_DEPTH 3
#define KND_ID_BASE 64

#define KND_MAX_INT_SIZE 4

#define KND_MAX_DEBUG_CONTEXT_SIZE 100

#define KND_ID_SIZE  (4 * sizeof(char))
#define KND_ID_BATCH_SIZE 10
#define KND_LOCALE_SIZE 8

#define KND_STATE_SIZE  (4 * sizeof(char))

#define KND_MAX_MIGRATIONS 256
#define KND_MAX_SPECS 64

#define KND_MAX_CONC_CHILDREN 128
#define KND_MAX_BASES 128

#define KND_DIR_SIZE_ENCODE_BASE 10
#define KND_DIR_ENTRY_SIZE 16
#define KND_MAX_ARGS 16
#define KND_DIR_TRAILER_MAX_SIZE 1024 * 1024 * 10

#define KND_MAX_BACKREFS 128

#define KND_MATCH_MAX_SCORE 100
#define KND_MATCH_SCORE_THRESHOLD 0.65
#define KND_MAX_MATCHES 512
#define KND_MATCH_MAX_RESULTS 10


#define KND_SID_SIZE 128
#define KND_TID_SIZE 128

#define KND_MAX_TIDS 1024
#define KND_MAX_USERS 1024 * 1024

#define KND_MAX_GEOIPS 160000
#define KND_MAX_GEO_LOCS 3200

/* KND Object */
#define KND_OBJ_METABUF_SIZE 1024

#define KND_DEFAULT_CLASS_DEPTH 3
#define KND_MAX_CLASS_DEPTH 3
#define KND_MAX_CLASS_BATCH 128

#define KND_DEFAULT_OBJ_DEPTH 2
#define KND_MAX_OBJ_DEPTH 4

#define KND_MAX_FLAT_ROWS 256
#define KND_MAX_FLAT_COLS 64

/* tags */
#define KND_OBJ_MAIN_REC_TAG "M"
#define KND_OBJ_MAIN_REC_TAG_SIZE strlen(KND_OBJ_MAIN_REC_TAG)

#define KND_OBJ_NAME_TAG "N"
#define KND_OBJ_NAME_TAG_SIZE strlen(KND_OBJ_NAME_TAG)

/* access policy */
#define KND_OBJ_POLICY_TAG "A"
#define KND_OBJ_POLICY_TAG_SIZE strlen(KND_OBJ_POLICY_TAG)

#define KND_OBJ_MIMETYPE_TAG "MI"
#define KND_OBJ_MIMETYPE_TAG_SIZE strlen(KND_OBJ_MIMETYPE_TAG)

#define KND_OBJ_FILENAME_TAG "FN"
#define KND_OBJ_FILENAME_TAG_SIZE strlen(KND_OBJ_FILENAME_TAG)

#define KND_OBJ_FILESIZE_TAG "FS"
#define KND_OBJ_FILESIZE_TAG_SIZE strlen(KND_OBJ_FILESIZE_TAG)

#define KND_OBJ_ROOT_TAG "R"
#define KND_OBJ_ROOT_TAG_SIZE strlen(KND_OBJ_ROOT_TAG)

#define KND_OBJ_LIST_TAG "L"
#define KND_OBJ_LIST_TAG_SIZE strlen(KND_OBJ_LIST_TAG)

#define KND_OBJ_ELEM_TAG "E"
#define KND_OBJ_ELEM_TAG_SIZE strlen(KND_OBJ_ELEM_TAG)


#define KND_NUMFIELD_MAX_SIZE 8

#define KND_REFSET_TAG "F"
#define KND_REFSET_TAG_SIZE strlen(KND_REFSET_TAG)

#define KND_REFSET_DIR_TAG "D"
#define KND_REFSET_DIR_TAG_SIZE strlen(KND_REFSET_DIR_TAG)

#define KND_REFSET_FEAT_TAG "fe"
#define KND_REFSET_FEAT_TAG_SIZE strlen(KND_REFSET_FEAT_TAG)

#define KND_REFSET_INBOX_TAG "^"
#define KND_REFSET_INBOX_TAG_SIZE strlen(KND_REFSET_INBOX_TAG)

#define KND_REFSET_TERM_TAG "_"
#define KND_REFSET_TERM_TAG_SIZE strlen(KND_REFSET_TERM_TAG)

#define KND_REFSET_REF_TAG "R"
#define KND_REFSET_REF_TAG_SIZE strlen(KND_REFSET_REF_TAG)

#define KND_REFSET_HEADWORD_TAG "H"
#define KND_REFSET_HEADWORD_TAG_SIZE strlen(KND_REFSET_HEADWORD_TAG)

#define KND_DATABAND_TAG "B"
#define KND_DATABAND_TAG_SIZE strlen(KND_DATABAND_TAG)

#define KND_TIMEPERIOD_TAG "P"
#define KND_TIMEPERIOD_TAG_SIZE strlen(KND_TIMEPERIOD_TAG)


#define KND_LOC_SEPAR "/"
#define KND_FACET_SEPAR "#"
#define KND_TEXT_CHUNK_SEPAR " "

/* index */
#define KND_IDX_MAIN_REC_TAG "0"
#define KND_IDX_MAIN_REC_TAG_SIZE strlen(KND_IDX_MAIN_REC_TAG)

#define GSL_SPEC_TAG "S"
#define GSL_SPEC_TAG_SIZE strlen(GSL_SPEC_TAG)

#define GSL_LOC_TAG "L"
#define GSL_LOC_TAG_SIZE strlen(GSL_LOC_TAG)

#define GSL_TRANSLIT_TAG "TR"
#define GSL_TRANSLIT_TAG_SIZE strlen(GSL_TRANSLIT_TAG)

#define GSL_OPEN_DELIM "("
#define GSL_OPEN_DELIM_SIZE strlen(GSL_OPEN_DELIM)

#define GSL_CLOSE_DELIM ")"
#define GSL_CLOSE_DELIM_SIZE strlen(GSL_CLOSE_DELIM)

#define GSL_OPEN_FACET_DELIM "["
#define GSL_OPEN_FACET_DELIM_SIZE strlen(GSL_OPEN_FACET_DELIM)

#define GSL_CLOSE_FACET_DELIM "]"
#define GSL_CLOSE_FACET_DELIM_SIZE strlen(GSL_CLOSE_FACET_DELIM)

#define GSL_TERM_SEPAR ":"
#define GSL_TERM_SEPAR_SIZE strlen(GSL_TERM_SEPAR)

#define GSL_OFFSET "@"
#define GSL_OFFSET_SIZE strlen(GSL_OFFSET)

#define GSL_TOTAL "="
#define GSL_TOTAL_SIZE strlen(GSL_OFFSET)

#define GSL_CONC_SEPAR "|"
#define GSL_CONC_SEPAR_SIZE strlen(GSL_CONC_SEPAR)

#define KND_FIELD_SEPAR ";"
#define KND_FIELD_SEPAR_SIZE strlen(KND_FIELD_SEPAR)



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
#define KND_MAX_CLAUSES 32
#define KND_MAX_ELEMLOCS 128


#define KND_LEAF_SIZE 10

/* number of leafs to read from index at once */
#define KND_LEAF_CHUNK_SIZE 100

#define KND_SIZE_OF_OFFSET sizeof(size_t)

/* alphanumeric symbols:
   0-9, A-Z, a-z */
#define KND_RADIX_BASE 62
#define KND_ID_MAX_COUNT KND_RADIX_BASE * KND_RADIX_BASE * KND_RADIX_BASE

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

#define KND_REFSET_MAX_DEPTH 10
#define KND_TEMP_BUF_SIZE 1024
#define KND_MED_BUF_SIZE 1024 * 10

#define KND_LARGE_BUF_SIZE 1024 * 1024 * 100
#define KND_SMALL_BUF_SIZE 64
#define KND_LABEL_SIZE 8
#define KND_PATH_SIZE 1024
#define KND_NAME_SIZE 512
#define KND_SHORT_NAME_SIZE 64
#define KND_VAL_SIZE 128

#define KND_UID_SIZE 7

#define KND_CONC_NAME_BUF_SIZE 1024

#define KND_SEARCH_BUF_SIZE 1024 * 100 * sizeof(char)
#define KND_IDX_BUF_SIZE 10 * 1024 * 1024 * sizeof(char)

#define KND_SPECS_BUF_SIZE 1024 * 100 * sizeof(char)

#define KND_LOC_REC_SIZE (sizeof(unsigned long) + sizeof(unsigned long))

#define KND_MAX_CONC_UNITS 10000
#define KND_MAX_CONTEXTS 4
#define KND_MAX_HILITE_CONCUNITS 256

#define KND_LOCSET_BUF_SIZE KND_MAX_CONC_UNITS * KND_LOC_REC_SIZE

#define KND_MAX_TEXT_BUF_SIZE 1024 * 1024 * sizeof(char)
#define KND_MAX_METADATA_BUF_SIZE 1024 * 10 * sizeof(char)
#define KND_MAX_OBJ_BUF_SIZE 1024 * 100 * sizeof(char)
#define KND_MAX_ERR_MSG_BUF_SIZE 1024 * 10 * sizeof(char)
#define KND_MAX_UPDATE_BUF_SIZE 1024 * 100 * sizeof(char)

#define KND_MIN_CLASSES 1024
#define KND_MIN_OBJS 1024
#define KND_MIN_ELEMS 1024
#define KND_MIN_RELS 1024
#define KND_MIN_REL_INSTANCES 1024
#define KND_MIN_PROCS 1024
#define KND_MIN_PROC_INSTANCES 1024

#define KND_MAX_TEXT_CHUNK_SIZE 1024 * 10
#define KND_MAX_DEBUG_CHUNK_SIZE 256

#define KND_MAX_CONTEXTS 4

#define KND_REFSETS_BATCH_SIZE 32
#define KND_MAX_REFSETS 128

#define KND_MAX_INBOX_SIZE 8
#define KND_REFSET_MIN_SIZE KND_MAX_INBOX_SIZE / 10
#define KND_INBOX_RESERVE_RATIO 1.4

#define KND_THRESHOLD_RATIO 0.85

#define KND_FEATURED_SIZE 5
#define KND_FEATURED_MAX_SIZE 10

#define KND_SIMILAR_BATCH_SIZE 5
#define KND_SIMILAR_MAX_SIZE 30

#define KND_SPECS_BATCH_SIZE 5
#define KND_SPECS_MAX_SIZE 30

#define KND_MAX_RESULT_SIZE (1024 * sizeof(char))
#define KND_CONTEXT_RADIUS 100

/* locset */
#define KND_LOCREC_NUM_FIELDS 2
#define KND_LOCREC_FIELD_SIZE sizeof(unsigned long)
#define KND_LOCREC_SIZE (KND_LOCREC_FIELD_SIZE * KND_LOCREC_NUM_FIELDS)



#define KND_DELIVERY_OK "OK"
#define KND_DELIVERY_OK_SIZE strlen(KND_DELIVERY_OK)

#define KND_DELIVERY_NO_RESULTS "NULL"
#define KND_DELIVERY_NO_RESULTS_SIZE strlen(KND_DELIVERY_NO_RESULTS)

#define KND_JSON_RESULTS_BEGIN "{\"results\":["
#define KND_JSON_RESULTS_BEGIN_SIZE strlen(KND_JSON_RESULTS_BEGIN)

#define KND_JSON_RESULTS_END "]}"
#define KND_JSON_RESULTS_END_SIZE 2

#define KND_JSON_FIRST_RESULT_ITEM "{\"text\":\""
#define KND_JSON_NEXT_RESULT_ITEM ",{\"text\":\""

#define KND_JSON_TITLE ",\"title\": \""
#define KND_JSON_TITLE_SIZE strlen(KND_JSON_TITLE)

#define KND_JSON_GUID ",\"guid\": \"%s\""

#define KND_JSON_TOTAL_MATCHES ",\"total\": \"%lu\""

#define KND_JSON_AUTH ",\"auth\": \""
#define KND_JSON_AUTH_SIZE strlen(KND_JSON_AUTH)

#define KND_JSON_EXACT ",\"exact\": ["
#define KND_JSON_EXACT_SIZE strlen(KND_JSON_EXACT)

#define KND_JSON_EXACT_NAME ",\"exact_name\": \"%s\""

#define KND_JSON_TEXT ",\"text\": \""
#define KND_JSON_TEXT_SIZE strlen(KND_JSON_TEXT)

#define KND_JSON_SIMILAR ",\"similar\": ["
#define KND_JSON_SIMILAR_SIZE strlen(KND_JSON_SIMILAR)

#define KND_JSON_ROLES ",\"roles\": \""
#define KND_JSON_ROLES_SIZE strlen(KND_JSON_ROLES)

#define KND_JSON_SPECS ",\"specs\": \""
#define KND_JSON_SPECS_SIZE strlen(KND_JSON_SPECS)

#define KND_JSON_GUIDS ",\"guids\": ["
#define KND_JSON_GUIDS_SIZE strlen(KND_JSON_SPECS)

#define KND_JSON_MORE_SIMILAR "],\"more_similar\": ["
#define KND_JSON_MORE_SIMILAR_SIZE strlen(KND_JSON_MORE_SIMILAR)

#define KND_JSON_MORE_SPECS ",\"more_specs\": \""
#define KND_JSON_MORE_SPECS_SIZE strlen(KND_JSON_MORE_SPECS)

#define KND_JSON_STR_END "\""
#define KND_JSON_STR_END_SIZE 1

#define KND_JSON_ARRAY_BEGIN "["
#define KND_JSON_ARRAY_BEGIN_SIZE 1

#define KND_JSON_ARRAY_END "]"
#define KND_JSON_ARRAY_END_SIZE 1

#define KND_JSON_DICT_END "}"
#define KND_JSON_DICT_END_SIZE 1

#define KND_JSON_SEPAR ","
#define KND_JSON_SEPAR_SIZE 1

#endif
