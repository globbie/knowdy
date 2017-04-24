/**
 *   Copyright (c) 2011-2015 by Dmitri Dmitriev
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
 *   knd_conc.c
 *   Knowdy Concept implementation
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <sys/stat.h>

#include <unistd.h>

#include "knd_config.h"

#include "knd_conc.h"
#include "knd_repo.h"
#include "knd_dataclass.h"
#include "knd_object.h"
#include "knd_coderef.h"
#include "knd_sorttag.h"

#include "knd_output.h"
#include "knd_refset.h"
#include "knd_query.h"

#include "knd_data_reader.h"

#define DEBUG_KND_CONC_LEVEL_1 0
#define DEBUG_KND_CONC_LEVEL_2 0
#define DEBUG_KND_CONC_LEVEL_3 0
#define DEBUG_KND_CONC_LEVEL_4 0
#define DEBUG_KND_CONC_LEVEL_5 0
#define DEBUG_KND_CONC_LEVEL_TMP 1

static int
kndConc_str(struct kndConc *self, 
            size_t depth __attribute__((unused)))
{
    knd_log("%s\n", self->name);
    return knd_OK;
}

static int 
kndConc_del(struct kndConc *self)
{

    free(self);

    return knd_OK;
}



static int 
kndConc_export_JSON(struct kndConc *self __attribute__((unused)))
{
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;

    /*
    struct kndElemLoc *elemloc;

    int i, err;
    
    for (i = 0; i < self->num_elemlocs; i++) {
        elemloc = self->elemlocs;

        
        buf_size = sprintf(buf, "{\"elemloc\": \"%s\", \"objs\": [",
                           elemloc->name);
        
        err = self->maze->write(self->maze, KND_SEARCH_RESULTS, buf, buf_size);
        if (err) return err;

        err = elemloc->refcoll->export(elemloc->refcoll, KND_FORMAT_JSON);
        if (err) return err;

        err = self->maze->write(self->maze,
                                KND_SEARCH_RESULTS, 
                                KND_JSON_RESULTS_END,
                                KND_JSON_RESULTS_END_SIZE);
        if (err) return err;


        if (i + 1 == self->num_elemlocs) break;
        

        err = self->maze->write(self->maze,
                                KND_SEARCH_RESULTS, 
                                KND_JSON_SEPAR,
                                KND_JSON_SEPAR_SIZE);
        if (err) return err;

                           
    }
    */
    
    /*knd_log("MAZE OUTPUT: %s [size: %lu]\n\n",
      self->maze->results, (unsigned long)self->maze->results_size); */
    
    return knd_OK;
}


static int 
kndConc_export(struct kndConc *self,
                knd_format format)
{
    switch (format) {
    case KND_FORMAT_JSON:
        kndConc_export_JSON(self);
        break;
    default:
        break;
    }
    
    return knd_OK;
}





static int
kndConc_get_conc(struct kndConc     *self __attribute__((unused)),
                 const char         *name,
                 size_t              name_size,
                 struct kndConc     **result)
{
    struct kndConc *conc = NULL;
    struct kndRefSet *refset;
    int err = knd_FAIL;
    
    /* get conc */
    /* ask in-memory cache */

    // fixme
    /*conc = self->maze->conc_idx->get(self->maze->conc_idx,
                                      name);
    */
    
    if (!conc) {
        err = kndConc_new(&conc);
        if (err) goto final;
    


        /* assign name */
        memcpy(conc->name, name, name_size);
        conc->name[name_size] = '\0';

        err = kndRefSet_new(&refset);
        if (err) goto final;

        memcpy(refset->name, "/", 1);
        refset->name_size = 1;
    

        conc->refset = refset;
        
        /* register in idx */
        /*err = self->maze->conc_idx->set(self->maze->conc_idx,
                                         name,
                                         (void*)conc);
        if (err) goto final;

        self->maze->num_conc_refs++; */
        
    }

    *result = conc;
    err = knd_OK;
    
 final:    
    return err;
}


static int
kndConc_parse(struct kndConc     *self,
              const char         *rec,
              size_t             rec_size,
              struct kndElemRef  **result)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    struct kndElemRef *elemref = NULL;
    struct kndCodeRef *coderef = NULL;
    
    const char *b, *c, *s;
    
    //bool rec_is_valid = false;

    bool in_rec = false;
    bool in_elemref = false;
    
    //long parsed_num = 0;
    size_t curr_size = 0;
    
    int err = knd_FAIL;
    
    /* parse rec */
    b = rec;
    c = rec;
    
    while (*c) {
        switch (*c) {
        case '^':
            buf_size = c - b;
            memcpy(buf, b, buf_size);
            buf[buf_size] = '\0';

            if (!strcmp(buf, "L")) {
                elemref = malloc(sizeof(struct kndElemRef));
                if (!elemref) {
                    err = knd_NOMEM;
                    goto final;
                }
                memset(elemref, 0, sizeof(struct kndElemRef));
                
                in_elemref = true;
            }
            
            b = c + 1;
            break;

        case '(':
            if (!in_rec) {
                in_rec = true;
                b = c + 1;
                break;
            }

        case '[':
            err = kndCodeRef_new(&coderef);
            if (err) goto final;

            curr_size = c - rec;
            err = coderef->parse(coderef, c, rec_size - curr_size);
            if (err) goto final;
            
            goto assign;
        case ')':
            if (in_elemref) {
                /* skip over path attr name */
                s = strchr(b, ':');
                if (s) b = s + 1;

                elemref->name_size = c - b;
                if (elemref->name_size >= KND_NAME_SIZE) {
                    err = knd_LIMIT;
                    goto final;
                }
                    
                memcpy(elemref->name, b, elemref->name_size);
                elemref->name[elemref->name_size] = '\0';
                
                /* get elem suffix */
                s = c;
                while (s > b) {
                    if ((*s) == '_') {
                        buf_size = c - s;
                        memcpy(buf, s, buf_size);
                        buf[buf_size] = '\0';

                        /*knd_log("ELEMREF type: \"%s\" REST: %s\n\n", buf, c);*/

                        /* atomic value */
                        if (!strcmp(buf, "_a")) {
                            c++;
                            err =  kndCodeRef_new(&coderef);
                            if (err) goto final;
                            coderef->type = KND_ELEM_ATOM;

                            /* TODO */
                            
                            /*err = coderef->parse(coderef, buf, buf_size);
                            if (err) goto final;
                            */
                            
                            coderef->name_size = strlen(c);
                            memcpy(coderef->name, c, coderef->name_size);
                            coderef->name[coderef->name_size] = '\0';
                            
                            coderef->conc = self;
                            coderef->out = self->out;
                        }


                        break;
                    }
                    s--;
                }
                break;
            }
            
            if (in_rec) {
                in_rec = false;
                b = c + 1;
                break;
            }

        default:
            break;
        }
        
        c++;
    }

    if (!elemref) {
        err = knd_FAIL;
        goto final;
    }
    
    if (!coderef) {
        err = knd_FAIL;
        goto final;
    }
    
 assign:
    coderef->next = elemref->coderefs;
    elemref->coderefs = coderef;

    elemref->num_coderefs++;
    
    *result = elemref;
    
    return knd_OK;
    
 final:

    if (elemref) {
        free(elemref);
        /*elemref->del(elemref);*/
    }
    
    if (coderef)
        coderef->del(coderef);
    
    return err;
}

/*
static int
kndConc_register_concpath(struct kndConc   *self,
                          struct kndCodeRef *coderef)
{
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;

    //struct kndRefSet *refset;
    //char *b, *c;
    int err = knd_FAIL;

    if (DEBUG_KND_CONC_LEVEL_TMP)
        knd_log("\n      ... register CONC PATH: %s\n\n", coderef->concpath);

    return err;
}
*/

static int
kndConc_import(struct kndConc   *self,
               struct kndObject *obj,
               const char       *rec,
               size_t            rec_size)
{
    //char buf[KND_MED_BUF_SIZE];
    //size_t buf_size;

    struct kndElemRef *elemref;
    struct kndDataElem *elem;
    
    struct kndCodeRef *coderef;
    struct kndConc *conc = NULL;

    struct kndRefSet *refset;
    //struct kndSpecRole *role;

    int err = knd_FAIL;
    
    if (DEBUG_KND_CONC_LEVEL_TMP)
        knd_log("\n      ... CONC IMPORT: %s\n\n", rec);

    err = kndConc_parse(self,
                        rec, rec_size,
                        &elemref);
    if (err) return err;

    if (DEBUG_KND_CONC_LEVEL_TMP)
        knd_log("   == ELEMREF: %s\n\n", elemref->name);

    elem = obj->cache->baseclass->elems;
    /*while (elem) {
        
        if (!strncmp(elemref->name, elem->path, elem->path_size)) {

            
            elemref->elem = elem;
            break;
        }
        elem = elem->next;
    }
    */
    
    coderef = elemref->coderefs;
    if (coderef->type == KND_ELEM_ATOM) {

        if (DEBUG_KND_CONC_LEVEL_TMP)
            knd_log("\n\n    .. register atomic val \"%s\"..\n",
                    coderef->name);
        
        err = kndConc_get_conc(self,
                               (const char*)coderef->name,
                               coderef->name_size,
                               &conc);
        if (err) goto final;
        
        refset = conc->refset;

        /* TODO: add facets */
        /*err = refset->import(refset,
                             obj, obj->facet_buf, elemref);

        knd_log("FACET: %s\n", obj->facet_buf);
        */
        
        if (DEBUG_KND_CONC_LEVEL_TMP) {
            knd_log("\n   ++ RefSet of atomic value is updated! TOTAL: %lu\n\n",
                (unsigned long)refset->num_refs);
            /*refset->str(refset, 1, 6);*/
        }

        err = knd_OK;
        goto final;
    }
    
    coderef->out = self->out;
    self->out->reset(self->out);
                
    err = coderef->export(coderef, 0, KND_FORMAT_GSL);
    if (err) goto final;

    /*if (DEBUG_KND_CONC_LEVEL_TMP)
        knd_log("\n\n    .. conc to IDX: \"%s\"\n", self->maze->idx);
    
    err = kndConc_get_conc(self,
                           (const char*)self->maze->idx,
                           self->maze->idx_size,
                           &conc);
    if (err) goto final;
    */
    
    /* update refset */
    refset = conc->refset;

    /*err = refset->import(refset,
                         obj, obj->facet_buf, elemref);
    if (err) goto final;
    */
    
    /*if (DEBUG_KND_CONC_LEVEL_3) {
        knd_log("\n   ++ RefSet of complex conc \"%s\" is updated! TOTAL: %lu\n\n",
                self->maze->idx, (unsigned long)refset->num_refs);
        refset->str(refset, 1, 6);
    }
    */

    /* TODO: set refs from constituents to complex */
    

    return knd_OK;
    
final:

    return err;
}



static int
kndConc_read(struct kndConc   *self,
             char             *rec,
             size_t            rec_size,
             struct kndQuery  *q __attribute__((unused)))
{
    char buf[KND_LARGE_BUF_SIZE];
    size_t buf_size;

    struct kndRefSet *refset;
    
    bool in_rec = false;
    bool in_conc = false;
    bool in_refset = false;

    struct kndConc *conc = NULL;
    
    char *b, *c, *s;
    //long numval;
    int err = knd_FAIL;

    if (DEBUG_KND_CONC_LEVEL_TMP)
        knd_log("\n\n   .. Reading CONC IDX rec, input size [%lu]: %s\n",
                (unsigned long)rec_size, rec);

    /* parse rec */
    b = rec;
    c = rec;
    
    while (*c) {
        switch (*c) {
        case '\n':

            if (!in_rec)
                break;
            
            if (in_conc) {
                in_conc = false;
                in_refset = true;
            }
            else {
                in_conc = true;
                in_refset = false;
            }
            
            break;
        case '(':
            if (!in_rec) {
                in_rec = true;
                in_conc = true;
                break;
            }

            if (in_refset) {
                s = strchr(c, '\n');

                if (!s) 
                    s = rec + rec_size;
                
                buf_size = s - c;
                if (buf_size >= sizeof(buf)) {
                    knd_log("   -- not enough MEM: input chunk requires %lu bytes :(\n",
                            (unsigned long)buf_size);
                    err = knd_LIMIT;
                    goto final;
                }
                    
                memcpy(buf, c, buf_size);
                buf[buf_size] = '\0';

                if (DEBUG_KND_CONC_LEVEL_TMP)
                    knd_log("  ..refset rec: \"%s\" [%lu]\n",
                            buf, (unsigned long)buf_size);
                
                /* add refset to conc */
                refset = conc->refset;

                err = refset->read(refset, buf, buf_size);
                if (err) goto final;

                conc = NULL;
                c = s - 1;
                in_refset = false;
                break;
            }
            b = c;
            break;
        case '[':
            if (in_conc) {

                s = strchr(c, '\n');
                if (!s) {
                    err = knd_FAIL;
                    goto final;
                }
                buf_size = s - c - 1;
                memcpy(buf, c, buf_size);
                buf[buf_size] = '\0';
                
                if (DEBUG_KND_CONC_LEVEL_TMP)
                    knd_log("   .. parse CONC IDX rec: \"%s\"\n", buf);

                err = kndConc_get_conc(self, buf, buf_size, &conc);
                if (err) goto final;
                
                c = s - 1;
                break;
            }
            
            break;
        default:
            break;
        }
        c++;
    }

    err = knd_OK;

final:

    /*if (conc)
      free(conc); */
    
    return err;
}


static int
kndConc_open(struct kndConc *self)
{
    char filename[KND_TEMP_BUF_SIZE];
    size_t filename_size = 0;

    const char *dbfile = "conc.gsl";
    size_t dbfile_size = strlen(dbfile);

    struct stat file_info;
    size_t filesize;
    char *file = NULL;
    int fd;
    int err = knd_FAIL;
    
    /*filename_size = self->maze->cache->repo->path_size + dbfile_size;*/
    // fixme
    if (filename_size >= KND_TEMP_BUF_SIZE) return knd_NOMEM;
    
    /*strcpy(filename, self->maze->cache->repo->path);*/
    
    strncat(filename, "/", 1);
    strncat(filename, dbfile, dbfile_size);

    if (DEBUG_KND_CONC_LEVEL_TMP)
        knd_log("  open CONC IDX: %s ..\n", filename);
            
    /* DB file doesn't exists */
    fd = open(filename, O_RDONLY);
    knd_log("  status: %d\n", fd);
    
    if (fd < 0) {
        if (DEBUG_KND_CONC_LEVEL_3)
            knd_log("   -- no such file: \"%s\"\n",
                    filename);
        return knd_OK;
    }
    
    fstat(fd, &file_info);
    filesize = file_info.st_size;  
    
    if (DEBUG_KND_CONC_LEVEL_TMP)
        knd_log("  .. reading FILE \"%s\" [%lu] ...\n",
                filename, (unsigned long)filesize);

    /* TODO: copy to buffer */
    file = malloc(sizeof(char) * filesize);
    if (!file) {
        err = knd_NOMEM;
        goto final;
    }

    err = read(fd, file, filesize);
    if (err == -1) {
        err = knd_IO_FAIL;
        goto final;
    }

    if (DEBUG_KND_CONC_LEVEL_TMP)
        knd_log("   ++ FILE \"%s\" read OK!\n", filename);

    err = kndConc_read(self, file, filesize, NULL);
    if (err) goto final;
    
 final:
    if (file)
        free(file);
    
    close(fd);
    
    return err;
}



static int
kndConc_sync(struct kndConc   *self __attribute__((unused)))
{
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size;

    char filename[KND_TEMP_BUF_SIZE];
    //size_t filename_size;

    struct stat st;

    //struct kndConc *conc;
    //struct kndRefSet *refset;


    //const char *key = NULL;
    //void *val = NULL;

    //const char *dbfile = "conc.gsl";
    //size_t dbfile_size = strlen(dbfile);
    
    int err = knd_FAIL;

    /*knd_log("DB PATH SIZE: %lu\n",
            (unsigned long)self->maze->cache->repo->path_size);
    
    filename_size = self->maze->cache->repo->path_size + dbfile_size;
    if (filename_size >= KND_TEMP_BUF_SIZE) return knd_NOMEM;
    
    strcpy(filename, self->maze->cache->repo->path);
    strncat(filename, "/", 1);
    strncat(filename, dbfile, dbfile_size);
    */
    
    /*knd_log("DBFILE: %s\n", filename);*/
            
    /* file exists, remove it */
    if (!stat(filename, &st)) {
        err = remove(filename);
        if (err) goto final;
        knd_log("  -- Existing file removed..\n");
    }


    /*    self->maze->conc_idx->rewind(self->maze->conc_idx);
    do {
        self->maze->conc_idx->next_item(self->maze->conc_idx, &key, &val);
        if (!key) break;
        
        conc = (struct kndConc*)val;

        if (key[0] == '[') 
            buf_size = sprintf(buf,
                               "\n(C%s)\n", key);
        else {
            buf_size = sprintf(buf,
                               "\n(C[%s])\n", key);
        }
        
        if (DEBUG_KND_CONC_LEVEL_TMP)
            knd_log("\n   .. sync conc: \"%s\"\n", buf);
        
        err = knd_append_file((const char*)filename,
                              buf,
                              buf_size);
        if (err) goto final;

        refset = conc->refset;

        refset->maze->reset(refset->maze);


    */        
        /*refset->str(refset, 1, 5);*/

    /*
        err = refset->sync(refset);
        if (err) goto final;
    */  
        /*err = knd_append_file((const char*)filename,
                              refset->maze->idx,
                              refset->maze->idx_size);
        if (err) goto final;
        */
        
        /*knd_log("\n  == CONC: \"%s\" [%lu]\n",
                conc->name, (unsigned long)refset->num_refs);
                knd_log("    REFSET: %s\n", refset->maze->idx); */
        
    /*    }
    while (key);
    */
    
    knd_log(" ++ conc sync OK!\n");
    
 final:    
    return err;
}


extern int
kndConc_init(struct kndConc *self)
{
    memset(self, 0, sizeof(struct kndConc));

    self->del       = kndConc_del;
    self->str       = kndConc_str;
    self->import    = kndConc_import;
    self->export    = kndConc_export;
    self->sync      = kndConc_sync;
    self->open      = kndConc_open;

    return knd_OK;
}

/* constructor */
extern int
kndConc_new(struct kndConc **conc)
{
    //struct kndSet *set;
    //size_t *locset;
    //struct kndConcept *c;

    struct kndConc *self = malloc(sizeof(struct kndConc));
    if (!self) return knd_NOMEM;

    kndConc_init(self);
 
    *conc = self;

    return knd_OK;
}
