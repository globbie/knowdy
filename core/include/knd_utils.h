#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "knd_config.h"

//typedef enum output_dest_t { KND_SEARCH_RESULTS,
//			     KND_IDX,
//			     KND_OBJ_META,
//                             KND_OBJ_REPR,
//			     KND_USER_META,
//                             KND_USER_REPR,
//                             KND_DBREC,
//                             KND_ERR_MSG,
//                             KND_UPDATE,
//			     KND_TOPICS,
//			     KND_SPECS  } output_dest_t;


//extern int knd_compare(const char *a, const char *b);
//extern int knd_inc_id(char *id);
//extern int knd_is_valid_id(const char *id, size_t id_size);
//extern int knd_state_is_valid(const char *id, size_t id_size);
//extern int knd_next_state(char *s);
extern void  knd_calc_num_id(const char *id, size_t id_size, size_t *numval);
extern void knd_num_to_str(size_t numval, char *buf, size_t *buf_size, size_t base);

static inline void knd_gsp_num_to_num(const char *val, size_t val_size, size_t *num) {
    knd_calc_num_id(val, val_size, num);
}

static inline void knd_num_to_gsp_num(size_t num, char *out_val, size_t *out_val_size) {
    knd_num_to_str(num, out_val, out_val_size, KND_RADIX_BASE);
}

static inline void knd_uid_create(size_t seed, char *out_uid, size_t *out_uid_size) {
    knd_num_to_str(seed, out_uid, out_uid_size, KND_RADIX_BASE);
}

//extern const char *max_id(const char *a, const char *b);
//extern const char *min_id(const char *a, const char *b);

extern int knd_mkpath(const char *path, size_t path_size, mode_t mode, bool has_filename);

extern int knd_write_file(const char *filename, 
                          void *buf, size_t buf_size);

extern int 
knd_append_file(const char *filename, 
                const void *buf, size_t buf_size);

extern int knd_make_id_path(char *buf,
		     const char *path,
		     const char *id, 
		     const char *filename);

extern int  knd_get_elem_suffix(const char *name,
                                char *buf);
//extern unsigned char * knd_pack_int(unsigned char *buf,
//                                    unsigned int val);
//extern unsigned long knd_unpack_int(const unsigned char *buf);

extern void knd_remove_nonprintables(char *data);

extern void knd_log(const char *fmt, ...);

extern int obj_id_base[256];
extern const char *obj_id_seq;

extern int knd_read_UTF8_char(const char *rec,
                              size_t rec_size,
                              size_t *val,
                              size_t *len);

extern int knd_parse_num(const char *val,
                         long *result);

extern int knd_read_name(char *output,
                         size_t *output_size,
                         const char *rec,
                         size_t rec_size);

extern int knd_parse_IPV4(char *ip, unsigned long *ip_val);

extern int knd_get_schema_name(const char *rec,
                               char *buf,
                               size_t *buf_size,
                               size_t *total_size);
extern int knd_parse_incipit(const char *rec,
                             size_t rec_size,
                             char *result_tag_name,
                             size_t *result_tag_name_size,
                             char *result_name,
                             size_t *result_name_size);

extern int knd_parse_dir_size(const char *rec,
                              size_t rec_size,
                              const char **val,
                              size_t *val_size,
                              size_t *total_trailer_size);

