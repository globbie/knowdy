#ifndef KND_UTILS_H
#define KND_UTILS_H

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <libxml/parser.h>

#include "../knd_config.h"
#include "oodict.h"

typedef enum output_dest_t { KND_SEARCH_RESULTS, 
			     KND_IDX, 
			     KND_OBJ_META,
                             KND_OBJ_REPR,
			     KND_USER_META,
                             KND_USER_REPR,
                             KND_DBREC,
                             KND_ERR_MSG,
                             KND_UPDATE,
			     KND_TOPICS,
			     KND_SPECS  } output_dest_t;

struct kndData {
    unsigned long ip;
    knd_format format;
    
    char tid[KND_TID_SIZE + 1];
    size_t tid_size;

    char sid[KND_TEMP_BUF_SIZE];
    size_t sid_size;

    char uid[KND_ID_SIZE + 1];
    size_t uid_size;
    
    char user_agent[KND_TEMP_BUF_SIZE];
    size_t user_agent_size;

    char header[KND_TEMP_BUF_SIZE];
    size_t header_size;

    char meta[KND_MED_BUF_SIZE];
    size_t meta_size;

    char classname[KND_NAME_SIZE];
    size_t classname_size;

    char guid[KND_ID_SIZE + 1];

    char name[KND_NAME_SIZE];
    size_t name_size;

    char lang_id[KND_NAME_SIZE];
    size_t lang_id_size;

    char *spec;
    size_t spec_size;

    char *obj;
    size_t obj_size;

    char *body;
    size_t body_size;

    char *topics;
    size_t topic_size;

    char *index;
    size_t index_size;

    char *query;
    size_t query_size;

    char *reply;
    size_t reply_size;

    char mimetype[KND_NAME_SIZE];
    size_t mimetype_size;

    char filename[KND_NAME_SIZE];
    size_t filename_size;
    size_t filesize;

    char *filepath;
    size_t filepath_size;
    
    char *results;
    size_t result_size;
    size_t num_results;

    char *control_msg;
    size_t control_msg_size;

    const char *ref;
    size_t ref_size;

    int (*del)(struct kndData *self);
    int (*reset)(struct kndData *self);

};

extern int kndData_new(struct kndData **self);


extern int knd_compare(const char *a, const char *b);
extern int knd_inc_id(char *id);
extern int knd_is_valid_id(const char *id, size_t id_size);

extern const char *max_id(const char *a, const char *b);
extern const char *min_id(const char *a, const char *b);

extern int knd_mkpath(const char *path, mode_t mode, bool has_filename);

extern int 
knd_write_file(const char *path, const char *filename, 
               void *buf, size_t buf_size);

extern int 
knd_append_file(const char *filename, 
                void *buf, size_t buf_size);

extern int knd_make_id_path(char *buf,
		     const char *path,
		     const char *id, 
		     const char *filename);

extern int 
knd_get_elem_suffix(const char *name,
                    char *buf);


extern unsigned char *
knd_pack_int(unsigned char *buf,
             unsigned int val);

extern unsigned long
knd_unpack_int(const unsigned char *buf);

extern int 
knd_get_trailer(const char  *rec,
                size_t rec_size,
                char  *name,
                size_t *name_size,
                size_t *num_items,
                char   *dir_rec,
                size_t *dir_rec_size);
extern int 
knd_get_conc_prefix(const char *name,
		    size_t name_size,
		    char *prefix);
extern int
knd_copy_xmlattr(xmlNode    *input_node,
		 const char *attr_name,
		 char       **result,
		 size_t     *result_size);

extern int
knd_get_xmlattr(xmlNode    *input_node,
		 const char *attr_name,
		 char       *result,
		 size_t     *result_size);

extern int
knd_get_xmlattr_num(xmlNode *input_node,
		    const char *attr_name,
		    long *result);


extern int
knd_get_attr(const char *text,
	     const char *attr,
	     char *value,
	     size_t *val_size);


extern int 
knd_read_UTF8_char(const char *rec,
                   size_t rec_size,
                   size_t *val,
                   size_t *len);

extern int 
knd_parse_matching_braces(const char *rec,
                          size_t *chunk_size);

extern int 
knd_remove_nonprintables(char *data);

extern int
knd_parse_num(const char *val,
	      long *result);
extern int
knd_read_name(char *output,
              size_t *output_size,
              const char *rec,
              size_t rec_size);
extern int
knd_parse_IPV4(char *ip, unsigned long *ip_val);

extern void 
knd_log(const char *fmt, ...);

extern int obj_id_base[256];
extern const char *obj_id_seq;

#endif

