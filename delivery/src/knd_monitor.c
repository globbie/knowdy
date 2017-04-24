#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <libxml/parser.h>

#include <knd_refset.h>
#include <knd_sorttag.h>
#include <knd_output.h>

#include "knd_monitor.h"

/* forward */
static struct kndTrans*
kndMonitor_alloc_trn(struct kndMonitor *self);

static struct kndGeoIP*
kndMonitor_get_geoip(struct kndMonitor *self,
                     struct kndGeoIP *node,
                     unsigned long numval);

static int
kndMonitor_str(struct kndMonitor *self)
{
    knd_log("<struct kndMonitor at %p>\n", self);


    return knd_OK;
}


static int
kndMonitor_del(struct kndMonitor *self)
{
    free(self);
    return knd_OK;
}

/*
static int
kndMonitor_lookup(struct kndMonitor *self,
                  struct kndData    *data)
{

    knd_log("  Monitor lookup\n");

    //if (trn->geoip) {
    //        geoloc = self->geo_locs[trn->geoip->city_code];
    //        memcpy(buf, geoip->country_code, 2);
    //        buf[2] = '\0';
    //        knd_log("CITY NAME: %s\n", geoloc->name);
    //    }
    //    }

    return knd_OK;
}
*/

static struct kndGeoIP*
kndMonitor_get_geoip(struct kndMonitor *self,
                     struct kndGeoIP *node,
                     unsigned long numval)
{
    if (!node) return NULL;

    /*knd_log("lookup %lu in node (from: %lu, to: %lu)\n", numval, node->from, node->to); */
    
    if (node->from > numval) {

        return kndMonitor_get_geoip(self, node->lt, numval);
    }

    if (node->to < numval)
        return kndMonitor_get_geoip(self, node->gt, numval);

    return node;
}

static struct kndGeoIP*
kndMonitor_add_node(struct kndMonitor *self,
                    unsigned long start,
                    unsigned long end)
{
    struct kndGeoIP *node = NULL;
    unsigned long middle;
    
    if (start > end) return NULL;

    middle = start + (end - start) / 2;
    
    node = self->geoips[middle];
    if (!node) return NULL;

    if (middle) {
        node->lt = kndMonitor_add_node(self, start, middle - 1);
        node->gt = kndMonitor_add_node(self, middle + 1, end);
    }
    
    return node;
}


static int
kndMonitor_read_cities(struct kndMonitor *self)
{
    char buf[KND_TEMP_BUF_SIZE];
    FILE *f;

    struct kndGeoLoc *geoloc;
    
    char *field;
    const char *delim = "\t";
    char *last = NULL;
    long numval;
    float pos;
    int i = 0, err;

    f = fopen(self->geo_names, "r");
    if (!f) return -1;

    while (fgets(buf, KND_TEMP_BUF_SIZE, f)) {
        i = 0;

        geoloc = malloc(sizeof(struct kndGeoLoc));
        if (!geoloc) return knd_NOMEM;

        memset(geoloc, 0, sizeof(struct kndGeoLoc));
        
        geoloc->type = KND_GEO_CITY;
        
        for (field = strtok_r(buf, delim, &last);
             field;
             field = strtok_r(NULL, delim, &last)) {

            switch (i) {
            case 0:
                err = knd_parse_num(field, &numval);
                if (err) return err;
                geoloc->id = numval;
                break;
            case 1:
                /*knd_log("CITY: %s\n", field);*/
                strcpy(geoloc->city, field);
                break;
            case 2:
                strcpy(geoloc->district, field);
                break;
            case 4:
                sscanf(field, "%f", &pos);
                geoloc->lat = pos;
                break;
            case 5:
                sscanf(field, "%f", &pos);
                geoloc->lng = pos;
                break;
            default:
                break;
            }
            i++;
        }


        if (geoloc->id >= KND_MAX_GEO_LOCS)
            return knd_FAIL;

        
        self->geo_locs[geoloc->id] = geoloc;

    }
    
    return knd_OK;
}

static int
kndMonitor_read_geo(struct kndMonitor *self,
                    xmlNodePtr input_node)
{
    char buf[KND_TEMP_BUF_SIZE];
    FILE *f;
    size_t curr_size;

    struct kndGeoIP *geoip;
    
    char *field;
    const char *delim = "\t";
    char *last = NULL;
    long numval;

    int i = 0, err;
    
    err = knd_copy_xmlattr(input_node, "db", 
			   &self->geo_db, &curr_size);
    if (err) return err;


    err = knd_copy_xmlattr(input_node, "names", 
			   &self->geo_names, &curr_size);
    if (err) return err;

    knd_log("   == GEO IP DB: %s NAMES: %s\n", self->geo_db, self->geo_names);

    f = fopen(self->geo_db, "r");
    if (!f) return -1;

    while (fgets(buf, KND_TEMP_BUF_SIZE, f)) {
        i = 0;

        geoip = malloc(sizeof(struct kndGeoIP));
        if (!geoip) return knd_NOMEM;

        memset(geoip, 0, sizeof(struct kndGeoIP));
        
        for (field = strtok_r(buf, delim, &last);
             field;
             field = strtok_r(NULL, delim, &last)) {

            switch (i) {
            case 0:
                err = knd_parse_num(field, &numval);
                if (err) return err;
                geoip->from = numval;
                break;
            case 1:
                err = knd_parse_num(field, &numval);
                if (err) return err;
                geoip->to = numval;
                break;
            case 3:
                memcpy(geoip->country_code, field, 2);
                break;
            case 4:

                if (field[0] == '-')
                    break;
                
                err = knd_parse_num(field, &numval);
                if (!err)
                    geoip->city_code = (unsigned int)numval;
                break;
                
            default:
                break;
            }
            i++;
        }

        if (self->num_geoips + 1 > self->max_geoips)
            return -1;
        
        self->geoips[self->num_geoips] = geoip;
        self->num_geoips++;
    }
    
    fclose(f);
    
    knd_log("TOTAL geoips: %lu\n", self->num_geoips);
    
    self->geo_bst = kndMonitor_add_node(self, 0, self->num_geoips - 1);

    err = kndMonitor_read_cities(self);
    
    return err;
}

static int
kndMonitor_datetime_idx(struct kndMonitor *self, 
                        struct kndTrans *trn)
{
    struct kndObjRef *ref;
    struct kndSortTag *tag;
    struct kndSortAttr *attr;
    int err = knd_FAIL;

    err = kndObjRef_new(&ref);
    if (err) return err;
    
    ref->type = KND_REF_TID;
    ref->trn = trn;

    err = kndSortTag_new(&tag);
    if (err) return err;
    attr = malloc(sizeof(struct kndSortAttr));
    if (!attr) return knd_NOMEM;
    memset(attr, 0, sizeof(struct kndSortAttr));
    tag->attrs[0] = attr;
    tag->num_attrs = 1;
    ref->sorttag = tag;

    attr->type = KND_FACET_ACCUMULATED;
    memcpy(attr->name, "D", 1);
    attr->name_size = 1;
    attr->val_size = strftime(attr->val,
                        KND_NAME_SIZE,
                        "y%y+m%m+d%d+h%H+m%M",
                        (const struct tm*)&trn->timeinfo);

    /* second attr: alphabetic */
    attr = malloc(sizeof(struct kndSortAttr));
    if (!attr) return knd_NOMEM;
    memset(attr, 0, sizeof(struct kndSortAttr));
    tag->attrs[1] = attr;
    tag->num_attrs = 2;

    attr->type = KND_FACET_POSITIONAL;
    memcpy(attr->name, "AZ", 2);
    attr->name_size = 2;

    strcpy(attr->val, trn->query);
    attr->val_size = strlen(trn->query);

    if (!strcmp(trn->query, "/"))
        attr->is_trivia = true;
        
    return self->datetime_browser->add_ref(self->datetime_browser, ref);
}


/*
static int
kndMonitor_query_idx(struct kndMonitor *self,
                     struct kndTrans *trn)
{
    struct kndObjRef *ref;
    struct kndSortTag *tag;
    struct kndSortAttr *attr;
    int err = knd_FAIL;

    err = kndObjRef_new(&ref);
    if (err) return err;
    
    ref->type = KND_REF_TID;
    ref->trn = trn;

    err = kndSortTag_new(&tag);
    if (err) return err;

    attr = malloc(sizeof(struct kndSortAttr));
    if (!attr) return knd_NOMEM;
    memset(attr, 0, sizeof(struct kndSortAttr));
    tag->attrs[0] = attr;
    tag->num_attrs = 1;
    ref->sorttag = tag;

    attr->type = KND_FACET_CATEGORICAL;
    memcpy(attr->name, "C", 1);
    attr->name_size = 1;

    strcpy(attr->val, trn->action);
    attr->val_size = trn->action_size;
    
    // second attr: alphabetic
    attr = malloc(sizeof(struct kndSortAttr));
    if (!attr) return knd_NOMEM;
    memset(attr, 0, sizeof(struct kndSortAttr));
    tag->attrs[1] = attr;
    tag->num_attrs = 2;

    attr->type = KND_FACET_POSITIONAL;
    memcpy(attr->name, "AZ", 2);
    attr->name_size = 2;
    
    strcpy(attr->val, trn->query);
    attr->val_size = strlen(trn->query);

    //knd_log("ALPHABETIC PART: \"%s\" [size: %lu]\n", attr->val, attr->val_size);

    return self->query_browser->add_ref(self->query_browser, ref);
}
*/

/*
static int
kndMonitor_geoloc_idx(struct kndMonitor *self, 
                      struct kndTrans *trn)
{
    struct kndObjRef *ref;
    struct kndSortTag *tag;
    struct kndSortAttr *attr;
    char *c;
    int err = knd_FAIL;

    err = kndObjRef_new(&ref);
    if (err) return err;
   
    ref->type = KND_REF_TID;
    ref->trn = trn;

    err = kndSortTag_new(&tag);
    if (err) return err;
    attr = malloc(sizeof(struct kndSortAttr));
    if (!attr) return knd_NOMEM;

    memset(attr, 0, sizeof(struct kndSortAttr));
    tag->attrs[0] = attr;
    tag->num_attrs = 1;
    ref->sorttag = tag;

    attr->type = KND_FACET_CATEGORICAL;
    memcpy(attr->name, "C", 1);
    attr->name_size = 1;

    memcpy(attr->val, "00", 2);
    attr->val_size = 2;
    
    if (trn->geoip) {
        c = attr->val;
        memcpy(c, trn->geoip->country_code, 2);
        c += 2;
        attr->val_size = 2;
    }
    
    //geoloc = self->geo_locs[trn->geoip->city_code];
    //memcpy(buf, geoip->country_code, 2);

    //memcpy(c, "|", 1);
    //c++;

    attr = malloc(sizeof(struct kndSortAttr));
    if (!attr) return knd_NOMEM;
    memset(attr, 0, sizeof(struct kndSortAttr));
    tag->attrs[1] = attr;
    tag->num_attrs = 2;

    attr->type = KND_FACET_ACCUMULATED;
    
    memcpy(attr->name, "D", 1);
    attr->name_size = 1;
    attr->val_size = strftime(attr->val,
                              KND_NAME_SIZE,
                              "y%y+m%m+d%d+h%H+m%M",
                              (const struct tm*)&trn->timeinfo);

    return self->geo_browser->add_ref(self->geo_browser, ref);
}
*/

static int
kndMonitor_process(struct kndMonitor *self, 
                   xmlNodePtr input_node,
                   struct kndData *data,
                   const char *action_name)
{
    char buf[KND_NAME_SIZE];
    size_t buf_size = KND_NAME_SIZE;

    struct kndTrans *trn;
    //struct kndGeoLoc *geoloc;
    
    //const char *query;
    struct kndGeoIP *geoip = NULL;

    time_t curr_time;
    int err;
    
    err = knd_get_xmlattr(input_node, "addr", 
                          buf, &buf_size);
    if (!err) {
        err = knd_parse_IPV4(buf, &data->ip);
        if (err) data->ip = 0;
    }

    geoip = self->default_geoip;

    if (data->ip) {
        geoip = kndMonitor_get_geoip(self, self->geo_bst, data->ip);
        if (!geoip)
            geoip = self->default_geoip;
    }

    
    /* TODO:
    data->user_agent_size = KND_NAME_SIZE;
    err = knd_get_xmlattr(input_node, "user-agent", 
                          data->user_agent, &data->user_agent_size);
    if (err) return err;
    */

    
    trn = kndMonitor_alloc_trn(self);
    if (!trn) return knd_NOMEM;
    
    memcpy(trn->tid, data->tid, KND_TID_SIZE);

    /* timestamp offset: 7 */
    memcpy(trn->uid, data->tid + 7, KND_UID_SIZE);

    trn->geoip = geoip;
    
    /* when */
    time(&curr_time);
    localtime_r(&curr_time, &trn->timeinfo);
     
    /* who */
    if (data->uid_size) {
        strcpy(trn->auth, data->uid);
    }
    else {
        strncpy(trn->auth, "000", strlen("000"));
    }
    
    /* action */
    strcpy(trn->action, action_name);
    trn->action_size = strlen(action_name);
    
    if (data->classname_size >= KND_NAME_SIZE) return knd_LIMIT;

    if (data->classname_size) {
        strncat(trn->action, "/", 1);
        strncat(trn->action, data->classname, data->classname_size);
    }

    strcpy(trn->query, "000");
    trn->query_size = strlen("000");
        
    if (data->query_size) {
        strcpy(trn->query, data->query);
        trn->query_size = data->query_size;
        
        if (!strcmp(data->query, "()")) {
            strcpy(trn->query, "/");
            trn->query_size = 1;
        }
    }
    
    /* obj name */
    if (data->name_size) {
        strcpy(trn->query, data->name);
        trn->query_size = data->name_size;
    }

    
    /*self->datetime_browser->str(self->datetime_browser, 1, 7);*/
    
    knd_log("\n\n  == system LOG monitor: TRN:%s QUERY:%s UID: %s\n  USER-AGENT: %s REFERER: %s\n\n",
            trn->action, trn->query, trn->uid, data->body, data->obj);
    
    err = self->idx->set(self->idx, 
                         (const char*)data->tid,
                         (void*)trn);
    if (err) return err;

    /* update indices */
    err = kndMonitor_datetime_idx(self, trn);
    if (err) return err;

    /*err = kndMonitor_query_idx(self, trn);
    if (err) return err;

    err = kndMonitor_geoloc_idx(self, trn);
    if (err) return err;
    */

    err = knd_OK;


    return err;
}



static int
kndMonitor_select(struct kndMonitor *self, 
                  xmlNodePtr input_node __attribute__((unused)),
                  struct kndData *data)
{
    struct kndOutput *out = self->out;
    int err;
    
    knd_log("\n\n  +++ Monitor SELECT in progress:\n");

    self->datetime_browser->str(self->datetime_browser, 1, 10);

    self->query_browser->str(self->query_browser, 1, 10);

    out->reset(out);

    err = out->write(out, "{\"repo\":{\"n\": \"monitor\",\"c_l\":[{\"n\":\"ooMonitor\"",
                   strlen("{\"repo\":{\"n\": \"monitor\",\"c_l\":[{\"n\":\"ooMonitor\""));
    if (err) return err;

    
    if (self->datetime_browser->num_refs) {
        err = out->write(out, ",\"browser\":", strlen(",\"browser\":"));
        if (err) return err;


        self->datetime_browser->out = out;
        self->datetime_browser->export_depth = 5;

        err = self->datetime_browser->export(self->datetime_browser, KND_FORMAT_JSON, 0);
        if (err) return err;
    }
    
    err = out->write(out, "}]}}", strlen("}]}}"));
    if (err) return err;

    knd_log("   == SELECT RESULT:\n %s\n\n", self->out->buf);
    
    data->ref = self->out->buf;
    data->ref_size = self->out->buf_size;
    
    return knd_OK;
}



static int
kndMonitor_sync(struct kndMonitor *self)
{
    //char buf[KND_TEMP_BUF_SIZE];
    //size_t buf_size = KND_TEMP_BUF_SIZE;

    //char recbuf[KND_TEMP_BUF_SIZE];
    //size_t recbuf_size = 0;

    struct kndTrans *trn;
    //const char *key = NULL;

    //void *value = NULL;
    size_t i;
    int err;


    
    knd_log("\n  +++ Monitor: sync in progress: %s\n",
            self->db_filename);
    
    /*err = knd_open_write_db(self->db_filename, 
      NULL,
                            &self->dbp);
                            if (err) return err;
    */
    
    for (i = 0; i < self->num_trns; i++) {
        trn = &self->trn_storage[i];
        
        
    }

    self->datetime_browser->del(self->datetime_browser);
    self->geo_browser->del(self->geo_browser);
    self->query_browser->del(self->query_browser);

    err = kndRefSet_new(&self->datetime_browser);
    if (err) return err;
    self->datetime_browser->name[0] = '/';
    self->datetime_browser->name_size = 1;

    err = kndRefSet_new(&self->geo_browser);
    if (err) return err;
    self->geo_browser->name[0] = '/';
    self->geo_browser->name_size = 1;

    err = kndRefSet_new(&self->query_browser);
    if (err) return err;
    self->query_browser->name[0] = '/';
    self->query_browser->name_size = 1;

    return knd_OK;
}

static int
kndMonitor_init(struct kndMonitor *self)
{
    self->str = kndMonitor_str;
    self->del = kndMonitor_del;
    self->process = kndMonitor_process;
    self->select = kndMonitor_select;
    self->sync = kndMonitor_sync;
    self->read_geo = kndMonitor_read_geo;
    
    return knd_OK;
}


/* allocate trn */
static
struct kndTrans*
kndMonitor_alloc_trn(struct kndMonitor *self)
{
    struct kndTrans *trn;
    int err;
    
    if (self->num_trns >= self->trn_storage_size) {

        knd_log( "    .. TRN storage capacity exceeded... flush data..\n");

        err = kndMonitor_sync(self);
        if (err) return NULL;

        self->num_trns = 0;
    }

    trn = &self->trn_storage[self->num_trns];

    /* init null values */
    memset(trn, 0, sizeof(struct kndTrans));

    self->num_trns++;

    return trn;
} 



extern int
kndMonitor_new(struct kndMonitor **rec)
{
    struct kndMonitor *self;
    int err;
    
    self = malloc(sizeof(struct kndMonitor));
    if (!self) return knd_NOMEM;

    memset(self, 0, sizeof(struct kndMonitor));

    /* allocate trns */
    self->trn_storage = malloc(sizeof(struct kndTrans) *\
				KND_TRN_STORAGE_SIZE);
    if (!self->trn_storage) {
	err = knd_NOMEM;
	goto error;
    }
    self->trn_storage_size = KND_TRN_STORAGE_SIZE;
    
    err = ooDict_new(&self->idx, KND_LARGE_DICT_SIZE);
    if (err) goto error;

    kndMonitor_init(self); 

    self->max_geoips = KND_MAX_GEOIPS;

    /*err = kndMonitor_read_config(self, config);
    if (err) goto error;
    */
    
    err = kndRefSet_new(&self->datetime_browser);
    if (err) goto error;
    self->datetime_browser->name[0] = '/';
    self->datetime_browser->name_size = 1;

    err = kndRefSet_new(&self->geo_browser);
    if (err) goto error;
    self->geo_browser->name[0] = '/';
    self->geo_browser->name_size = 1;

    err = kndRefSet_new(&self->query_browser);
    if (err) goto error;
    self->query_browser->name[0] = '/';
    self->query_browser->name_size = 1;

    self->default_geoip = malloc(sizeof(struct kndGeoIP));
    if (!self->default_geoip) return knd_NOMEM;

    memset(self->default_geoip, 0, sizeof(struct kndGeoIP));
    memcpy(self->default_geoip->country_code, "00", 2);

    *rec = self;

    return knd_OK;

 error:

    kndMonitor_del(self);

    return err;
}
