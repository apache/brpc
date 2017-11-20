#ifndef CETCD_CETCD_H
#define CETCD_CETCD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <curl/curl.h>
#include <pthread.h>
#include <stdint.h>
#include "sds/sds.h"
#include "cetcd_array.h"

#define CETCD_VERSION release-v0.0.5
typedef sds cetcd_string;
typedef pthread_t cetcd_watch_id;

enum HTTP_METHOD {
    ETCD_HTTP_GET,
    ETCD_HTTP_POST,
    ETCD_HTTP_PUT,
    ETCD_HTTP_DELETE,
    ETCD_HTTP_HEAD,
    ETCD_HTTP_OPTION
};
enum ETCD_EVENT_ACTION {
    ETCD_SET,
    ETCD_GET,
    ETCD_UPDATE,
    ETCD_CREATE,
    ETCD_DELETE,
    ETCD_EXPIRE,
    ETCD_CAS,
    ETCD_CAD,
    ETCD_ACTION_MAX
};
/* etcd error codes range is [100, 500]
 * We use 1000+ as cetcd error codes;
 * */
#define error_response_parsed_failed 1000
#define error_send_request_failed    1001
#define error_cluster_failed         1002
typedef struct cetcd_error_t {
    int ecode;
    char *message;
    char *cause;
    uint64_t index;
}cetcd_error;
typedef struct cetcd_client_t {
    CURL  *curl;
    cetcd_error *err;
    cetcd_array watchers; /*curl watch handlers*/
    cetcd_array *addresses;/*cluster addresses*/
    const char *keys_space;
    const char *stat_space;
    const char *member_space;
    int picked;
    struct {
        int      verbose;
        uint64_t ttl;
        uint64_t connect_timeout;
        uint64_t read_timeout;
        uint64_t write_timeout;
        char *user;
        char *password;
    } settings;

} cetcd_client;

typedef struct cetcd_response_node_t {
    cetcd_array *nodes; //struct etcd_response_node_t
    char *key;
    char *value;
    int dir; /* 1 for true, and 0 for false */
    uint64_t expiration;
    int64_t  ttl;
    uint64_t modified_index;
    uint64_t created_index;

} cetcd_response_node;

typedef struct cetcd_reponse_t {
    cetcd_error *err;
    int action;
    struct cetcd_response_node_t *node;
    struct cetcd_response_node_t *prev_node;
    uint64_t etcd_index;
    uint64_t raft_index;
    uint64_t raft_term;
} cetcd_response;

struct cetcd_response_parser_t ;

typedef int (*cetcd_watcher_callback) (void *userdata, cetcd_response *resp);
typedef struct cetcd_watcher_t {
    cetcd_client *cli;
    struct cetcd_response_parser_t *parser;
    int attempts;
    int array_index; /*the index in array cli->wachers*/

    CURL         *curl;
    int          once;
    int          recursive;
    uint64_t     index;
    char * key;
    void         *userdata;
    cetcd_watcher_callback callback;
} cetcd_watcher;

/*cetcd_client_create allocate the cetcd_client and return the pointer*/
cetcd_client* cetcd_client_create(cetcd_array *addresses);
/*cetcd_client_init initialize a cetcd_client*/
void          cetcd_client_init(cetcd_client *cli, cetcd_array *addresses);
/*cetcd_client_destroy destroy the resource a client used*/
void          cetcd_client_destroy(cetcd_client *cli);
/*cetcd_client_release free the cetcd_client object*/
void          cetcd_client_release(cetcd_client *cli);
/*cetcd_addresses_release free the array of an etcd cluster addresses*/
void          cetcd_addresses_release(cetcd_array *addrs);
/*cetcd_client_sync_cluster sync the members of an etcd cluster, this may be used
 * when the members of etcd changed
 * */
void          cetcd_client_sync_cluster(cetcd_client *cli);

/*cetcd_setup_user set the auth username and password*/
void          cetcd_setup_user(cetcd_client *cli, const char *user, const char *password);

/*cetcd_setup_tls setup the tls cert and key*/
void          cetcd_setup_tls(cetcd_client *cli, const char *CA,
        const char *cert, const char *key);

/*cetcd_get get the value of a key*/
cetcd_response *cetcd_get(cetcd_client *cli, const char *key);
/*cetcd_lsdir list the nodes under a directory*/
cetcd_response *cetcd_lsdir(cetcd_client *cli, const char *key, int sort, int recursive);

/*cetcd_set set the value of a key*/
cetcd_response *cetcd_set(cetcd_client *cli, const char *key,
        const char *value, uint64_t ttl);

/*cetcd_mkdir create a directroy, it will fail if the key has exist*/
cetcd_response *cetcd_mkdir(cetcd_client *cli, const char *key, uint64_t ttl);

/*cetcd_mkdir create a directory whether it exist or not*/
cetcd_response *cetcd_setdir(cetcd_client *cli, const char *key, uint64_t ttl);

/*cetcd_updatedir update the ttl of a directory*/
cetcd_response *cetcd_updatedir(cetcd_client *cli, const char *key, uint64_t ttl);

/*cetcd_update update the value or ttl of a key, only refresh the ttl if refresh is set*/
cetcd_response *cetcd_update(cetcd_client *cli, const char *key,
                             const char *value, uint64_t ttl, int refresh);
/*cetcd_create create a node with value*/
cetcd_response *cetcd_create(cetcd_client *cli, const char *key,
        const char *value, uint64_t ttl);
/*cetcd_create_in_order create in order keys*/
cetcd_response *cetcd_create_in_order(cetcd_client *cli, const char *key,
        const char *value, uint64_t ttl);
/*cetcd_delete delete a key*/
cetcd_response *cetcd_delete(cetcd_client *cli, const char *key);
/*cetcd_rmdir delete a directory*/
cetcd_response *cetcd_rmdir(cetcd_client *cli, const char *key, int recursive);

/*cetcd_watch watch the changes of a key*/
cetcd_response *cetcd_watch(cetcd_client *cli, const char *key, uint64_t index);
/*cetcd_watch_recursive watch a key and all its sub keys*/
cetcd_response *cetcd_watch_recursive(cetcd_client *cli, const char *key, uint64_t index);

cetcd_response *cetcd_cmp_and_swap(cetcd_client *cli, const char *key, const char *value,
        const char *prev, uint64_t ttl);
cetcd_response *cetcd_cmp_and_swap_by_index(cetcd_client *cli, const char *key, const char *value,
        uint64_t prev, uint64_t ttl);
cetcd_response *cetcd_cmp_and_delete(cetcd_client *cli, const char *key, const char *prev);
cetcd_response *cetcd_cmp_and_delete_by_index(cetcd_client *cli, const char *key, uint64_t prev);

/*cetcd_watcher_create create a watcher object*/
cetcd_watcher *cetcd_watcher_create(cetcd_client *cli, const char *key, uint64_t index,
        int recursive, int once, cetcd_watcher_callback callback, void *userdata);
/*cetcd_add_watcher add a watcher to the array*/
int cetcd_add_watcher(cetcd_array *watchers, cetcd_watcher *watcher);
/*cetcd_del_watcher delete a watcher from the array*/
int cetcd_del_watcher(cetcd_array *watchers, cetcd_watcher *watcher);

/*cetcd_multi_watch setup all watchers and wait*/
int cetcd_multi_watch(cetcd_client *cli, cetcd_array *watchers);
/*cetcd_multi_watch setup all watchers in a seperate thread and return the watch id*/
cetcd_watch_id cetcd_multi_watch_async(cetcd_client *cli, cetcd_array *watchers);
/*cetcd_multi_watch stop the watching thread with the watch id*/
int cetcd_multi_watch_async_stop(cetcd_client *cli, cetcd_watch_id wid);
/*cetcd_stop_watcher stop a watcher which has been setup*/
int cetcd_stop_watcher(cetcd_client *cli, cetcd_watcher *watcher);


void cetcd_response_print(cetcd_response *resp);
void cetcd_response_release(cetcd_response *resp);
void cetcd_error_release(cetcd_error *err);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
