#ifndef KND_MONITOR_H
#define KND_MONITOR_H

#include <time.h>

#include "../core/knd_utils.h"
#include "../core/oodict.h"

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

struct kndRefSet;

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


struct kndMonitor
{
    char *path;
    size_t path_size;
    
    char *service_address;

    char *db_filename;
    size_t db_filename_size;
    bool db_updated;

    struct kndDelivery *delivery;
    
    struct kndTrans *trn_storage;
    size_t num_trns;
    size_t trn_storage_size;

    char *geo_db;
    char *geo_names;
    struct kndGeoIP *geoips[KND_MAX_GEOIPS];
    size_t num_geoips;
    size_t max_geoips;
    
    struct kndGeoIP *geo_bst;

    struct kndGeoLoc *geo_locs[KND_MAX_GEO_LOCS];
    size_t num_geo_locs;
    
    struct kndGeoIP *geoip;
    
    struct kndGeoIP *default_geoip;
    
    struct ooDict *idx;

    struct ooDict *ua_idx;

    struct kndRefSet *datetime_browser;
    struct kndRefSet *geo_browser;
    struct kndRefSet *query_browser;
    struct kndRefSet *useragent_browser;
    struct kndRefSet *uid_browser;

    struct kndOutput *out;

    /**********  interface methods  **********/
    int (*del)(struct kndMonitor *self);
    int (*str)(struct kndMonitor *self);

    int (*process)(struct kndMonitor *self,
                   xmlNodePtr input_node,
                   struct kndData *data,
                   const char *action_name);

    int (*select)(struct kndMonitor *self,
                  xmlNodePtr input_node,
                  struct kndData *data);

    int (*read_geo)(struct kndMonitor *self,
                    xmlNodePtr input_node);

    int (*sync)(struct kndMonitor *self);

};

extern int kndMonitor_new(struct kndMonitor **self);
#endif
