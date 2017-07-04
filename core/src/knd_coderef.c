#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "knd_output.h"
#include "knd_conc.h"
#include "knd_coderef.h"
#include "knd_parser.h"

#define DEBUG_CODEREF_LEVEL_0 0
#define DEBUG_CODEREF_LEVEL_1 0
#define DEBUG_CODEREF_LEVEL_2 0
#define DEBUG_CODEREF_LEVEL_3 0
#define DEBUG_CODEREF_LEVEL_TMP 1

static int 
kndCodeRef_del(struct kndCodeRef *self)
{

    free(self);

    return knd_OK;
}

static int 
kndCodeRef_str(struct kndCodeRef *self, size_t depth)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    //size_t curr_size;
    char *c;
    size_t offset_size = sizeof(char) * KND_OFFSET_SIZE * depth;
    char *offset = malloc(offset_size + 1);
    //struct kndSortAttr *attr = NULL;

    memset(offset, ' ', offset_size);
    offset[offset_size] = '\0';

    c = buf;
    buf_size = 0;

    knd_log("%s == BASE \"%s\" POS: %lu LEN: %lu\n", offset,
            self->name,
            (unsigned long)self->linear_pos,
            (unsigned long)self->linear_len);

    if (self->spec) {
        knd_log("%s == SPEC \"%s\" POS: %lu LEN: %lu\n", offset,
                self->spec->name,
                (unsigned long)self->spec->linear_pos,
                (unsigned long)self->spec->linear_len);
    }
        
    return knd_OK;
}




static int
kndCodeRef_parse_term(struct kndCodeRef *self,
                      char *rec,
                      size_t rec_size __attribute__((unused)))
{
    //char buf[KND_NAME_SIZE];
    //size_t buf_size;

    long numval;
    
    const char *delim = ";";
    char *last;
    char *tok;
    char *val;
    
    bool pos_set = false;
    bool len_set = false;
    bool name_set = false;

    int err = knd_FAIL;

    for (tok = strtok_r(rec, delim, &last);
         tok;
         tok = strtok_r(NULL, delim, &last)) {

        val = strchr(tok, ':');
        if (!val) {
            err = knd_FAIL;
            break;
        }

        *val++ = '\0';
        
        /*knd_log("   ATTR: %s VAL: %s\n", tok, val);*/

        if (!strcmp(tok, "n")) {
            self->name_size = strlen(val);

            if (self->name_size >= sizeof(self->name)){
                err = knd_LIMIT;
                goto final;
            }

            memcpy(self->name, val, self->name_size);
            self->name[self->name_size] = '\0';
            name_set = true;
            continue;
        }

        if (!strcmp(tok, "c")) {
            self->concpath_size = strlen(val);

            if (self->concpath_size >= sizeof(self->concpath)){
                err = knd_LIMIT;
                goto final;
            }

            memcpy(self->concpath, val, self->concpath_size);
            self->concpath[self->concpath_size] = '\0';
            
            continue;
        }

        if (!strcmp(tok, "p")) {
            err = knd_parse_num((const char*)val, &numval);
            if (err) goto final;
            self->linear_pos = (size_t)numval;
            pos_set = true;
            continue;
        }

        if (!strcmp(tok, "l")) {
            err = knd_parse_num((const char*)val, &numval);
            if (err) goto final;
            self->linear_len = (size_t)numval;
            len_set = true;
            continue;
        }
    }
    
    if (name_set && pos_set && len_set)
        err = knd_OK;
    
 final:
    return err;
}


static int
kndCodeRef_parse_prop(struct kndCodeRef *self,
                      const char *rec,
                      size_t rec_size)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;

    //long numval;
    
    //const char *delim = ";";
    //char *last;
    //char *tok;
    //char *val;

    struct kndCodeRef *spec = NULL;
    
    const char *b;
    const char *c;
   
    //bool pos_set = false;
    //bool len_set = false;
    //bool name_set = false;

    //bool rec_is_valid = false;

    bool in_rec = false;
    bool in_complex = false;
    bool in_term = false;

    bool in_head = false;
    bool in_spec = false;

    int err = knd_FAIL;

    memcpy(buf, rec, rec_size);
    buf[rec_size] = '\0';

    if (DEBUG_CODEREF_LEVEL_3) {
        knd_log("\n\n.. parse CodeRef REC: \"%s\"\n",
                buf);
    }

    b = buf;
    c = b;

    while (*c) {
        switch (*c) {
        case '^':
            if (in_head)  {
                self->classname_size = c - b;
                memcpy(self->classname, b, self->classname_size);
                self->classname[self->classname_size] = '\0';
            }
            
            if (in_spec)  {
                spec->classname_size = c - b;
                memcpy(spec->classname, b, spec->classname_size);
                spec->classname[spec->classname_size] = '\0';

                if (DEBUG_CODEREF_LEVEL_3)
                    knd_log("  SPEC CLASS: \"%s\"\n", spec->classname);
                
                self->spec = spec;
            }

            in_term = true;
            b = c + 1;
            break;

        case '(':

            if (in_head) {
                in_head = false;
                in_spec = true;

                err = kndCodeRef_new(&spec);
                if (err) goto final;

                b = c + 1;
                break;
            }

            if (!in_rec) {
                in_rec = true;

                /*err =  kndCodeRef_new(&coderef);
                if (err) goto final;

                coderef->type = KND_ELEM_TEXT;

                coderef->conc = self;
                */
                
                b = c + 1;
                break;
            }

        case '[':

            if (in_spec) {
                self->spec_role_name_size = c - b;
                memcpy(self->spec_role_name, b, self->spec_role_name_size);
                self->spec_role_name[self->spec_role_name_size] = '\0';
                
                if (DEBUG_CODEREF_LEVEL_3)
                    knd_log("  SPEC ROLE: \"%s\"\n", self->spec_role_name);

                b = c + 1;
                break;
            }
            
            if (!in_complex) {
                in_complex = true;
                b = c + 1;
                break;
            }
            
            /* complex class */
            self->classname_size = c - b;
            memcpy(self->classname, b, self->classname_size);
            self->classname[self->classname_size] = '\0';

            in_head = true;
            b = c + 1;
            
            break;
        case ']':
            if (in_term) {

                buf_size = c - b;
                memcpy(buf, b, buf_size);
                buf[buf_size] = '\0';

                if (in_head) {
                    err = kndCodeRef_parse_term(self, buf, buf_size);
                    if (err) goto final;

                    if (DEBUG_CODEREF_LEVEL_3)
                        knd_log("  HEAD TERM: %s @%lu+%lu\n",
                                self->name,
                                (unsigned long)self->linear_pos,
                                (unsigned long)self->linear_len);
                }
                
                if (in_spec) {
                    err = kndCodeRef_parse_term(spec, buf, buf_size);
                    if (err) goto final;

                    if (DEBUG_CODEREF_LEVEL_3)
                        knd_log("  SPEC TERM: %s @%lu+%lu\n", spec->name,
                                (unsigned long)spec->linear_pos,
                                (unsigned long)spec->linear_len);
                }

                in_term = false;
                break;
            }

            if (in_head) {
                in_head = false;
                break;
            }
            
        case ')':
            if (in_term) {

                
                in_term = false;
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
    
 final:
    return err;
}

static int 
kndCodeRef_sync(struct kndCodeRef *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    //struct kndSortAttr *attr;
    int err;

    if (self->type == KND_ELEM_ATOM)
        return knd_OK;

    buf_size = sprintf(buf, "@%lu+%lu",
                       (unsigned long)self->linear_pos,
                       (unsigned long)self->linear_len);

    err = self->out->write(self->out, buf, buf_size);
    if (err) goto final;

    if (self->spec) {
        buf_size = sprintf(buf, ":%lu+%lu",
                           (unsigned long)self->spec->linear_pos,
                           (unsigned long)self->spec->linear_len);

        err = self->out->write(self->out, buf, buf_size);
        if (err) goto final;
    }

    
 final:
    
    return err;
}


static int 
kndCodeRef_export_JSON(struct kndCodeRef *self,
                        size_t depth)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct kndOutput *out = self->out;
    int err;

    if (depth) {
        err = out->write(out, ",", 1);
        if (err) goto final;
    }
    
    buf_size = sprintf(buf, "{\"n\":\"%s\",\"c\":\"base\",\"pos\":%lu,\"len\":%lu",
                       self->name,
                       (unsigned long)self->linear_pos,
                       (unsigned long)self->linear_len);
    err = out->write(out, buf, buf_size);
    if (err) goto final;

    err = out->write(out, "}", 1);
    if (err) goto final;
    
    /*depth + 1*/
    if (self->spec) {
        buf_size = sprintf(buf, ",{\"n\":\"%s\",\"c\":\"spec\",\"pos\":%lu,\"len\":%lu",
                           self->name,
                           (unsigned long)self->spec->linear_pos,
                           (unsigned long)self->spec->linear_len);
        err = out->write(out, buf, buf_size);
        if (err) goto final;
        
        err = out->write(out, "}", 1);
        if (err) goto final;
    }

    

 final:
    return err;
}

static int 
kndCodeRef_export_GSL(struct kndCodeRef *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    size_t buf_size;
    struct kndOutput *out = self->out;
    int err;

    if (!out) return knd_FAIL;

    buf_size = sprintf(buf, "[%s]",
                       self->name);
    err = out->write(out, buf, buf_size);
    if (err) goto final;

    if (self->spec) {
        buf_size = sprintf(buf, "(%s)",
                           self->spec->name);
        err = out->write(out, buf, buf_size);
        if (err) goto final;
    }

 final:
    return err;
}

static int 
kndCodeRef_export(struct kndCodeRef *self,
                   size_t depth,
                   knd_format format)
{
    int err = knd_FAIL;
    
    switch(format) {
    case KND_FORMAT_JSON:
        err = kndCodeRef_export_JSON(self, depth);
        if (err) goto final;
        break;
    case KND_FORMAT_GSL:
        err = kndCodeRef_export_GSL(self);
        if (err) goto final;
        break;
    default:
        break;
    }
 final:
    return err;
}



extern int 
kndCodeRef_new(struct kndCodeRef **objref)
{
    struct kndCodeRef *self;
    
    self = malloc(sizeof(struct kndCodeRef));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndCodeRef));

    self->del = kndCodeRef_del;
    self->str = kndCodeRef_str;
    self->export = kndCodeRef_export;
    self->parse = kndCodeRef_parse_prop;

    /*self->parse = kndCodeRef_parse_term;*/
    self->sync = kndCodeRef_sync;

    *objref = self;

    return knd_OK;
}
