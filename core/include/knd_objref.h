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
 *   ----------
 *   knd_objref.h
 *   Knowdy Object Reference
 */

#ifndef KND_OBJREF_H
#define KND_OBJREF_H

#include <time.h>


#include "knd_config.h"
#include "knd_conc.h"

struct kndDataClass;
struct kndOutput;
struct kndRepoCache;
struct kndCodeRef;
struct kndTrans;

typedef enum knd_ref_type { KND_REF_OBJ,
                            KND_REF_OBJ_SUBORD,
                            KND_REF_ELEM,
                            KND_REF_CODE,
                            KND_REF_TID,
                            KND_REF_ATOM } knd_ref_type;


typedef enum knd_geo_t { KND_GEO_COUNTRY, 
                         KND_GEO_DISTRICT, 
                         KND_GEO_REGION,
                         KND_GEO_CITY } knd_geo_t;

typedef enum knd_proc_state_t { KND_IN_PROCESS, 
                                KND_SUCCESS,
                                KND_NO_RESULTS,
                                KND_INTERNAL_ERROR } knd_proc_state_t;

typedef enum knd_ua_t { KND_UA_OTHER, 
                        KND_UA_ENGINE, 
                        KND_UA_BROWSER, 
                        KND_UA_OS,
                        KND_UA_COMPANY,
                        KND_UA_VERSION } knd_ua_t;


struct kndGeoIP
{
    unsigned long from;
    unsigned long to;

    char country_code[3];
    unsigned int city_code;
    
    struct kndGeoIP *lt;
    struct kndGeoIP *gt;
};


struct kndUserAgent
{
    knd_ua_t type;
    char name[KND_NAME_SIZE];
    
    struct kndUserAgent *children;
    struct kndUserAgent *next;
};


struct kndTrans
{
    char tid[KND_TID_SIZE + 1];

    struct tm timeinfo;

    char uid[KND_UID_SIZE + 1];
    char auth[KND_ID_SIZE + 1];

    char action[KND_NAME_SIZE];
    size_t action_size;
    
    char query[KND_NAME_SIZE];
    size_t query_size;
    
    unsigned long ip;
    struct kndGeoIP *geoip;
    struct kndUserAgent *user_agent;

    knd_proc_state_t proc_state;
};

struct kndGeoLoc
{
    unsigned long id;
    
    char city[KND_NAME_SIZE];
    char district[KND_NAME_SIZE];
    knd_geo_t type;

    float lat;
    float lng;

    struct kndGeoLoc *parent;

    struct kndGeoLoc *children;
    struct kndGeoLoc *next;
};


struct kndElemRef
{
    char name[KND_TEMP_BUF_SIZE];
    size_t name_size;

    size_t num_clauses;

    struct kndDataElem *elem;
    
    struct kndCodeRef *coderefs;
    size_t num_coderefs;
    
    struct kndElemRef *parent;

    struct kndElemRef *children;
    struct kndElemRef *next;
};



struct kndObjRef
{
    knd_ref_type type;
    
    char classname[KND_NAME_SIZE];
    size_t classname_size;

    char name[KND_NAME_SIZE];
    size_t name_size;

    char obj_id[KND_ID_SIZE + 1];
    size_t obj_id_size;

    struct kndDataClass *baseclass;
    struct kndObject *obj;
    struct kndTrans *trn;
    struct kndSortTag *sorttag;

    bool is_trivia;

    struct kndRepoCache *cache;
    
    /* for textual refs */
    struct kndElemRef *elemrefs;
 
    struct kndElem *title;
    struct kndElem *summary;

    /* visual emphasis of a chunk */
    size_t hilite_pos;
    size_t hilite_len;

    struct kndOutput *out;
    
    struct kndObjRef *next;
    
    /******** public methods ********/
    int (*init)(struct kndObjRef *self);
    void (*del)(struct kndObjRef *self);
    int (*str)(struct kndObjRef *self,
               size_t depth);


    int (*expand)(struct kndObjRef *self);

    int (*read_coderefs)(struct kndObjRef    *self,
                         struct kndElemRef *elemref,
                         const char *rec);

    int (*add_elemref)(struct kndObjRef    *self,
                       struct kndElemRef *elemref);

    int (*clone)(struct kndObjRef *self,
                 size_t attr_id,
                 const char *tail,
                 size_t tail_size,
                 struct kndObjRef **result);
    
    int (*reset)(struct kndObjRef *self);

    int (*sync)(struct kndObjRef *self);

    int (*import)(struct kndObjRef *self,
                  struct kndDataClass *baseclass,
                  char *rec);
    int (*export)(struct kndObjRef *self,
                   knd_format format);


};


/* constructor */
extern int kndObjRef_new(struct kndObjRef **self);

#endif /* KND_OBJREF_H */
