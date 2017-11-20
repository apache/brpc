#include "cetcd.h"
#include "cetcd_json_parser.h"

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <curl/curl.h>
#include <yajl/yajl_parse.h>
#include <sys/select.h>
#include <pthread.h>

enum ETCD_API_TYPE {
    ETCD_KEYS,
    ETCD_MEMBERS
};
typedef struct cetcd_request_t {
    enum HTTP_METHOD method;
    enum ETCD_API_TYPE api_type;
    cetcd_string uri;
    cetcd_string url;
    cetcd_string data;
    cetcd_client *cli;
} cetcd_request;
static const char *http_method[] = {
    "GET",
    "POST",
    "PUT",
    "DELETE",
    "HEAD",
    "OPTION"
};
typedef struct cetcd_response_parser_t {
    int st;
    int http_status;
    enum ETCD_API_TYPE api_type;
    cetcd_string buf;
    void *resp;
    yajl_parser_context ctx;
    yajl_handle json;
}cetcd_response_parser;
static const char *cetcd_event_action[] = {
    "set",
    "get",
    "update",
    "create",
    "delete",
    "expire",
    "compareAndSwap",
    "compareAndDelete"
};
void *cetcd_cluster_request(cetcd_client *cli, cetcd_request *req);
int cetcd_curl_setopt(CURL *curl, cetcd_watcher *watcher);

void cetcd_client_init(cetcd_client *cli, cetcd_array *addresses) {
    size_t i;
    cetcd_array *addrs;
    cetcd_string addr;
    curl_global_init(CURL_GLOBAL_ALL);
    srand(time(0));

    cli->keys_space =   "v2/keys";
    cli->stat_space =   "v2/stat";
    cli->member_space = "v2/members";
    cli->curl = curl_easy_init();

    addrs = cetcd_array_create(cetcd_array_size(addresses));
    for (i=0; i<cetcd_array_size(addresses); ++i) {
        addr = cetcd_array_get(addresses, i);
        if ( strncmp(addr, "http", 4)) {
            cetcd_array_append(addrs, sdscatprintf(sdsempty(), "http://%s", addr));
        } else {
            cetcd_array_append(addrs, sdsnew(addr));
        }
    }

    cli->addresses = cetcd_array_shuffle(addrs);
    cli->picked = rand() % (cetcd_array_size(cli->addresses));

    cli->settings.verbose = 0;
    cli->settings.connect_timeout = 1;
    cli->settings.read_timeout = 1;  /*not used now*/
    cli->settings.write_timeout = 1; /*not used now*/
    cli->settings.user = NULL;
    cli->settings.password = NULL;

    cetcd_array_init(&cli->watchers, 10);

    /* Set CURLOPT_NOSIGNAL to 1 to work around the libcurl bug:
     *  http://stackoverflow.com/questions/9191668/error-longjmp-causes-uninitialized-stack-frame
     *  http://curl.haxx.se/mail/lib-2008-09/0197.html
     * */
    curl_easy_setopt(cli->curl, CURLOPT_NOSIGNAL, 1L);

#if LIBCURL_VERSION_NUM >= 0x071900
    curl_easy_setopt(cli->curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(cli->curl, CURLOPT_TCP_KEEPINTVL, 1L); /*the same as go-etcd*/
#endif
    curl_easy_setopt(cli->curl, CURLOPT_USERAGENT, "cetcd");
    curl_easy_setopt(cli->curl, CURLOPT_POSTREDIR, 3L);     /*post after redirecting*/
    curl_easy_setopt(cli->curl, CURLOPT_SSLVERSION, CURL_SSLVERSION_TLSv1);
}
cetcd_client *cetcd_client_create(cetcd_array *addresses){
    cetcd_client *cli;

    cli = calloc(1, sizeof(cetcd_client));
    cetcd_client_init(cli, addresses);
    return cli;
}
void cetcd_client_destroy(cetcd_client *cli) {
    cetcd_addresses_release(cli->addresses);
    cetcd_array_release(cli->addresses);
    sdsfree(cli->settings.user);
    sdsfree(cli->settings.password);
    curl_easy_cleanup(cli->curl);
    curl_global_cleanup();
    cetcd_array_destroy(&cli->watchers);
}
void cetcd_client_release(cetcd_client *cli){
    if (cli) {
        cetcd_client_destroy(cli);
        free(cli);
    }
}
void cetcd_addresses_release(cetcd_array *addrs){
    int count, i;
    cetcd_string s;
    if (addrs) {
        count = cetcd_array_size(addrs);
        for (i = 0; i < count; ++i) {
            s = cetcd_array_get(addrs, i);
            sdsfree(s);
        }
    }
}
void cetcd_client_sync_cluster(cetcd_client *cli){
    cetcd_request req;
    cetcd_array *addrs;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_GET;
    req.api_type = ETCD_MEMBERS;
    req.uri = sdscatprintf(sdsempty(), "%s", cli->member_space);
    addrs =  cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    if (addrs == NULL) {
        return ;
    }
    cetcd_addresses_release(cli->addresses);
    cetcd_array_release(cli->addresses);
    cli->addresses = cetcd_array_shuffle(addrs);
    cli->picked = rand() % (cetcd_array_size(cli->addresses));
}
void cetcd_setup_user(cetcd_client *cli, const char* user, const char* password)
{
    if (user != NULL) {
        cli->settings.user = sdsnew(user);
    }
    if (password !=NULL) {
        cli->settings.password = sdsnew(password);
    }
}

void cetcd_setup_tls(cetcd_client *cli, const char *CA, const char *cert, const char *key) {
    if (CA) {
        curl_easy_setopt(cli->curl, CURLOPT_CAINFO, CA);
    }
    if (cert) {
        curl_easy_setopt(cli->curl, CURLOPT_SSLCERT, cert);
    }
    if (key) {
        curl_easy_setopt(cli->curl, CURLOPT_SSLKEY, key);
    }
}

size_t cetcd_parse_response(char *ptr, size_t size, size_t nmemb, void *userdata);

cetcd_watcher *cetcd_watcher_create(cetcd_client *cli, const char *key, uint64_t index,
        int recursive, int once, cetcd_watcher_callback callback, void *userdata) {
    cetcd_watcher *watcher;

    watcher = calloc(1, sizeof(cetcd_watcher));
    watcher->cli = cli;
    watcher->key = sdsnew(key);
    watcher->index = index;
    watcher->recursive = recursive;
    watcher->once = once;
    watcher->callback = callback;
    watcher->userdata = userdata;
    watcher->curl = curl_easy_init();

    watcher->parser = calloc(1, sizeof(cetcd_response_parser));
    watcher->parser->st = 0;
    watcher->parser->buf = sdsempty();
    watcher->parser->resp = calloc(1, sizeof(cetcd_response));

    watcher->array_index = -1;

    return watcher;
}
void cetcd_watcher_release(cetcd_watcher *watcher) {
    if (watcher) {
        if (watcher->key) {
            sdsfree(watcher->key);
        }
        if (watcher->curl) {
            curl_easy_cleanup(watcher->curl);
        }
        if (watcher->parser) {
            sdsfree(watcher->parser->buf);
            if (watcher->parser->json) {
                yajl_free(watcher->parser->json);
                cetcd_array_destroy(&watcher->parser->ctx.keystack);
                cetcd_array_destroy(&watcher->parser->ctx.nodestack);
            }
            cetcd_response_release(watcher->parser->resp);
            free(watcher->parser);
        }
        free(watcher);
    }
}

/*reset the temp resource one time watching used*/
void cetcd_watcher_reset(cetcd_watcher *watcher) {
    if (!watcher){
        return;
    }

    /*reset the curl handler*/
    curl_easy_reset(watcher->curl);
    cetcd_curl_setopt(watcher->curl, watcher);

    if (watcher->parser) {
        watcher->parser->st = 0;
        /*allocate the resp, because it is freed after calling the callback*/
        watcher->parser->resp = calloc(1, sizeof(cetcd_response));

        /*clear the buf, it is allocated by cetcd_watcher_create,
         * so should only be freed in cetcd_watcher_release*/
        sdsclear(watcher->parser->buf);

        /*the json object created by cetcd_parse_response, so it should be freed
         * after having got some response*/
        if (watcher->parser->json) {
            yajl_free(watcher->parser->json);
            cetcd_array_destroy(&watcher->parser->ctx.keystack);
            cetcd_array_destroy(&watcher->parser->ctx.nodestack);
            watcher->parser->json = NULL;
        }
    }
}
static cetcd_string cetcd_watcher_build_url(cetcd_client *cli, cetcd_watcher *watcher) {
    cetcd_string url;
    url = sdscatprintf(sdsempty(), "%s/%s%s?wait=true", (cetcd_string)cetcd_array_get(cli->addresses, cli->picked),
            cli->keys_space, watcher->key);
    if (watcher->index) {
        url = sdscatprintf(url, "&waitIndex=%lu", watcher->index);
    }
    if (watcher->recursive) {
        url = sdscatprintf(url, "&recursive=true");
    }
    return url;
}
int cetcd_curl_setopt(CURL *curl, cetcd_watcher *watcher) {
    cetcd_string url;

    url = cetcd_watcher_build_url(watcher->cli, watcher);
    curl_easy_setopt(curl,CURLOPT_URL, url);
    sdsfree(url);

    /* See above about CURLOPT_NOSIGNAL
     * */
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);

    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, watcher->cli->settings.connect_timeout);
#if LIBCURL_VERSION_NUM >= 0x071900
    curl_easy_setopt(watcher->curl, CURLOPT_TCP_KEEPALIVE, 1L);
    curl_easy_setopt(watcher->curl, CURLOPT_TCP_KEEPINTVL, 1L); /*the same as go-etcd*/
#endif
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "cetcd");
    curl_easy_setopt(curl, CURLOPT_POSTREDIR, 3L);     /*post after redirecting*/
    curl_easy_setopt(curl, CURLOPT_VERBOSE, watcher->cli->settings.verbose);

    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "GET");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cetcd_parse_response);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, watcher->parser);
    curl_easy_setopt(curl, CURLOPT_HEADER, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    curl_easy_setopt(curl, CURLOPT_PRIVATE, watcher);
    watcher->curl = curl;

    return 1;
}
int cetcd_add_watcher(cetcd_array *watchers, cetcd_watcher *watcher) {
    cetcd_watcher *w;

    cetcd_curl_setopt(watcher->curl, watcher);

    watcher->attempts = cetcd_array_size(watcher->cli->addresses);
    /* We use an array to store watchers. It will cause holes when remove some watchers.
     * watcher->array_index is used to reset to the original hole if the watcher was deleted before.
     * */
    if (watcher->array_index == -1) {
        cetcd_array_append(watchers, watcher);
        watcher->array_index = cetcd_array_size(watchers) - 1;
    } else {
        w = cetcd_array_get(watchers, watcher->array_index);
        if (w) {
            cetcd_watcher_release(w);
        }
        cetcd_array_set(watchers, watcher->array_index, watcher);
    }
    return 1;
}
int cetcd_del_watcher(cetcd_array *watchers, cetcd_watcher *watcher) {
    int index;
    index = watcher->array_index;
    if (watcher && index >= 0) {
        cetcd_array_set(watchers, index, NULL);
        cetcd_watcher_release(watcher);
    }
    return 1;
}
int cetcd_stop_watcher(cetcd_client *cli, cetcd_watcher *watcher) {
    /* Clear the callback function pointer to ensure to stop notify the user
     * Set once to 1 indicates that the watcher would stop after next trigger.
     *
     * The watcher object would be freed by cetcd_reap_watchers
     * Watchers may hang forever if it would be never triggered after set once to 1
     * FIXME: Cancel the blocking watcher
     * */
    UNUSED(cli);
    watcher->callback = NULL;
    watcher->once = 1;
    return 1;
}
static int cetcd_reap_watchers(cetcd_client *cli, CURLM *mcurl) {
    uint64_t index;
    int     added, ignore;
    CURLMsg *msg;
    CURL    *curl;
    cetcd_string url;
    cetcd_watcher *watcher;
    cetcd_response *resp;
    added = 0;
    index = 0;
    while ((msg = curl_multi_info_read(mcurl, &ignore)) != NULL) {
        if (msg->msg == CURLMSG_DONE) {
            curl = msg->easy_handle;
            curl_easy_getinfo(curl, CURLINFO_PRIVATE, &watcher);

            resp = watcher->parser->resp;
            index = watcher->index;
            if (msg->data.result != CURLE_OK) {
                /*try next in round-robin ways*/
                /*FIXME There is a race condition if multiple watchers failed*/
                if (watcher->attempts) {
                    cli->picked = (cli->picked+1)%(cetcd_array_size(cli->addresses));
                    url = cetcd_watcher_build_url(cli, watcher);
                    curl_easy_setopt(watcher->curl, CURLOPT_URL, url);
                    sdsfree(url);
                    curl_multi_remove_handle(mcurl, curl);
                    watcher->parser->st = 0;
                    curl_easy_reset(curl);
                    cetcd_curl_setopt(curl, watcher);
                    curl_multi_add_handle(mcurl, curl);
                    /*++added;
                     *watcher->attempts --;
                     */
                    continue;
                } else {
                    resp->err = calloc(1, sizeof(cetcd_error));
                    resp->err->ecode = error_cluster_failed;
                    resp->err->message = sdsnew("cetcd_reap_watchers: all cluster servers failed.");
                }
            }
            if (watcher->callback) {
                watcher->callback(watcher->userdata, resp);
                if (resp->err && resp->err->ecode != 401/* not outdated*/) {
                    curl_multi_remove_handle(mcurl, curl);
                    cetcd_watcher_release(watcher);
                    break;
                }
                if (resp->node) {
                    index = resp->node->modified_index;
                } else {
                    ++ index;
                }
                cetcd_response_release(resp);
                watcher->parser->resp = NULL; /*surpress it be freed again by cetcd_watcher_release*/
            }
            if (!watcher->once) {
                curl_multi_remove_handle(mcurl, curl);
                cetcd_watcher_reset(watcher);

                if (watcher->index) {
                    watcher->index = index + 1;
                    url = cetcd_watcher_build_url(cli, watcher);
                    curl_easy_setopt(watcher->curl, CURLOPT_URL, url);
                    sdsfree(url);
                }
                curl_multi_add_handle(mcurl, watcher->curl);
                ++added;
                continue;
            }
            curl_multi_remove_handle(mcurl, curl);
            cetcd_watcher_release(watcher);
        }
    }
    return added;
}
int cetcd_multi_watch(cetcd_client *cli, cetcd_array *watchers) {
    int           i, count;
    int           maxfd, left, added;
    long          timeout;
    long          backoff, backoff_max;
    fd_set        r, w, e;
    cetcd_watcher *watcher;
    CURLM         *mcurl;

    struct timeval tv;

    mcurl = curl_multi_init();
    count = cetcd_array_size(watchers);
    for (i = 0; i < count; ++i) {
        watcher = cetcd_array_get(watchers, i);
        curl_easy_setopt(watcher->curl, CURLOPT_PRIVATE, watcher);
        curl_multi_add_handle(mcurl, watcher->curl);
    }
    backoff = 100; /*100ms*/
    backoff_max = 1000; /*1 sec*/
    for(;;) {
        curl_multi_perform(mcurl, &left);
        if (left) {
            FD_ZERO(&r);
            FD_ZERO(&w);
            FD_ZERO(&e);

            curl_multi_timeout(mcurl, &timeout);
            if (timeout == -1) {
                timeout = 100; /*wait for 0.1 seconds*/
            }
            tv.tv_sec = timeout/1000;
            tv.tv_usec = (timeout%1000)*1000;

            curl_multi_fdset(mcurl, &r, &w, &e, &maxfd);

            /*TODO handle errors*/
            select(maxfd+1, &r, &w, &e, &tv);

            curl_multi_perform(mcurl, &left);
        }
        added = cetcd_reap_watchers(cli, mcurl);
        if (added == 0 && left == 0) {
        /* It will call curl_multi_perform immediately if:
         * 1. left is 0
         * 2. a new attempt should be issued
         * It is expected to sleep a mount time between attempts.
         * So we fix this by increasing added counter only
         * when a new request should be issued.
         * When added is 0, maybe there are retring requests or nothing.
         * Either situations should wait before issuing the request.
         * */
            if (backoff < backoff_max) {
                backoff = 2 * backoff;
            } else {
                backoff = backoff_max;
            }
            tv.tv_sec = backoff/1000;
            tv.tv_usec = (backoff%1000) * 1000;
            select(1, 0, 0, 0, &tv);
        }
    }
    curl_multi_cleanup(mcurl);
    return count;
}
static void *cetcd_multi_watch_wrapper(void *args[]) {
    cetcd_client *cli;
    cetcd_array  *watchers;
    cli = args[0];
    watchers = args[1];
    free(args);
    cetcd_multi_watch(cli, watchers);
    return 0;
}
cetcd_watch_id cetcd_multi_watch_async(cetcd_client *cli, cetcd_array *watchers) {
    pthread_t thread;
    void **args;
    args = calloc(2, sizeof(void *));
    args[0] = cli;
    args[1] = watchers;
    pthread_create(&thread, NULL, (void *(*)(void *))cetcd_multi_watch_wrapper, args);
    return thread;
}
int cetcd_multi_watch_async_stop(cetcd_client *cli, cetcd_watch_id wid) {
    (void) cli;
    /* Cancel causes the thread exit immediately, so the resouce has been
     * allocted won't be freed. The memory leak is OK because the process
     * is going to exit.
     * TODO fix the memory leaks
     * */
    pthread_cancel(wid);
    pthread_join(wid, 0);
    return 0;
}

cetcd_response *cetcd_get(cetcd_client *cli, const char *key) {
    return cetcd_lsdir(cli, key, 0, 0);
}

cetcd_response *cetcd_lsdir(cetcd_client *cli, const char *key, int sort, int recursive) {
    cetcd_request req;
    cetcd_response *resp;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_GET;
    req.uri = sdscatprintf(sdsempty(), "%s%s", cli->keys_space, key);
    if (sort) {
        req.uri = sdscatprintf(req.uri, "?sorted=true");
    }
    if (recursive){
        req.uri = sdscatprintf(req.uri, "%crecursive=true", sort?'&':'?');
    }
    resp = cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    return resp;
}

cetcd_response *cetcd_set(cetcd_client *cli, const char *key,
        const char *value, uint64_t ttl) {
    cetcd_request req;
    cetcd_response *resp;
    cetcd_string params;
    char *value_escaped;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_PUT;
    req.api_type = ETCD_KEYS;
    req.uri = sdscatprintf(sdsempty(), "%s%s", cli->keys_space, key);
    value_escaped = curl_easy_escape(cli->curl, value, strlen(value));
    params = sdscatprintf(sdsempty(), "value=%s", value_escaped);
    curl_free(value_escaped);
    if (ttl) {
        params = sdscatprintf(params, "&ttl=%lu", ttl);
    }
    req.data = params;
    resp = cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    sdsfree(params);
    return resp;
}

cetcd_response *cetcd_mkdir(cetcd_client *cli, const char *key, uint64_t ttl){
    cetcd_request req;
    cetcd_response *resp;
    cetcd_string params;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_PUT;
    req.api_type = ETCD_KEYS;
    req.uri = sdscatprintf(sdsempty(), "%s%s", cli->keys_space, key);
    params = sdscatprintf(sdsempty(), "dir=true&prevExist=false");
    if (ttl) {
        params = sdscatprintf(params, "&ttl=%lu", ttl);
    }
    req.data = params;
    resp = cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    sdsfree(params);
    return resp;
}

cetcd_response *cetcd_setdir(cetcd_client *cli, const char *key, uint64_t ttl){
    cetcd_request req;
    cetcd_response *resp;
    cetcd_string params;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_PUT;
    req.api_type = ETCD_KEYS;
    req.uri = sdscatprintf(sdsempty(), "%s%s", cli->keys_space, key);
    params = sdscatprintf(sdsempty(), "dir=true");
    if (ttl) {
        params = sdscatprintf(params, "&ttl=%lu", ttl);
    }
    req.data = params;
    resp = cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    sdsfree(params);
    return resp;
}

cetcd_response *cetcd_updatedir(cetcd_client *cli, const char *key, uint64_t ttl){
    cetcd_request req;
    cetcd_response *resp;
    cetcd_string params;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_PUT;
    req.api_type = ETCD_KEYS;
    req.uri = sdscatprintf(sdsempty(), "%s%s", cli->keys_space, key);
    params = sdscatprintf(sdsempty(), "dir=true&prevExist=true");
    if (ttl){
        params = sdscatprintf(params, "&ttl=%lu", ttl);
    }
    req.data = params;
    resp = cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    sdsfree(params);
    return resp;
}

cetcd_response *cetcd_update(cetcd_client *cli, const char *key,
                             const char *value, uint64_t ttl, int refresh) {
    cetcd_request req;
    cetcd_response *resp;
    cetcd_string params;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_PUT;
    req.api_type = ETCD_KEYS;
    req.uri = sdscatprintf(sdsempty(), "%s%s", cli->keys_space, key);
    params = sdscatprintf(sdsempty(), "prevExist=true");
    if (value) {
        char *value_escaped;
        value_escaped = curl_easy_escape(cli->curl, value, strlen(value));
        params = sdscatprintf(params, "&value=%s", value_escaped);
        curl_free(value_escaped);
    }
    if (ttl) {
        params = sdscatprintf(params, "&ttl=%lu", ttl);
    }
    if (refresh) {
        params = sdscatprintf(params, "&refresh=true");
    }
    req.data = params;
    resp = cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    sdsfree(params);
    return resp;
}

cetcd_response *cetcd_create(cetcd_client *cli, const char *key,
        const char *value, uint64_t ttl){
    cetcd_request req;
    cetcd_response *resp;
    cetcd_string params;
    char *value_escaped;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_PUT;
    req.api_type = ETCD_KEYS;
    req.uri = sdscatprintf(sdsempty(), "%s%s", cli->keys_space, key);
    value_escaped = curl_easy_escape(cli->curl, value, strlen(value));
    params = sdscatprintf(sdsempty(), "prevExist=false&value=%s", value_escaped);
    curl_free(value_escaped);
    if (ttl) {
        params = sdscatprintf(params, "&ttl=%lu", ttl);
    }
    req.data = params;
    resp = cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    sdsfree(params);
    return resp;
}

cetcd_response *cetcd_create_in_order(cetcd_client *cli, const char *key,
        const char *value, uint64_t ttl){
    cetcd_request req;
    cetcd_response *resp;
    cetcd_string params;
    char *value_escaped;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_POST;
    req.api_type = ETCD_KEYS;
    req.uri = sdscatprintf(sdsempty(),"%s%s", cli->keys_space, key);
    value_escaped = curl_easy_escape(cli->curl, value, strlen(value));
    params = sdscatprintf(sdsempty(), "value=%s", value_escaped);
    curl_free(value_escaped);
    if (ttl){
        params = sdscatprintf(params ,"&ttl=%lu", ttl);
    }
    req.data = params;
    resp = cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    sdsfree(params);
    return resp;
}

cetcd_response *cetcd_delete(cetcd_client *cli, const char *key) {
    cetcd_request req;
    cetcd_response *resp;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_DELETE;
    req.api_type = ETCD_KEYS;
    req.uri = sdscatprintf(sdsempty(), "%s%s", cli->keys_space, key);
    resp = cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    return resp;
}

cetcd_response *cetcd_rmdir(cetcd_client *cli, const char *key, int recursive){
    cetcd_request req;
    cetcd_response *resp;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_DELETE;
    req.api_type = ETCD_KEYS;
    req.uri = sdscatprintf(sdsempty(), "%s%s?dir=true", cli->keys_space, key);
    if (recursive){
        req.uri = sdscatprintf(req.uri, "&recursive=true");
    }
    resp = cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    return resp;
}

cetcd_response *cetcd_watch(cetcd_client *cli, const char *key, uint64_t index) {
    cetcd_request req;
    cetcd_response *resp;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_GET;
    req.api_type = ETCD_KEYS;
    req.uri = sdscatprintf(sdsempty(), "%s%s?wait=true&waitIndex=%lu", cli->keys_space, key, index);
    resp = cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    return resp;
}

cetcd_response *cetcd_watch_recursive(cetcd_client *cli, const char *key, uint64_t index) {
    cetcd_request req;
    cetcd_response *resp;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_GET;
    req.api_type = ETCD_KEYS;
    req.uri = sdscatprintf(sdsempty(), "%s%s?wait=true&recursive=true&waitIndex=%lu", cli->keys_space, key, index);
    resp = cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    return resp;
}

cetcd_response *cetcd_cmp_and_swap(cetcd_client *cli, const char *key, const char *value, const char *prev, uint64_t ttl) {
    cetcd_request req;
    cetcd_response *resp;
    cetcd_string params;
    char *value_escaped;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_PUT;
    req.api_type = ETCD_KEYS;
    req.uri = sdscatprintf(sdsempty(), "%s%s", cli->keys_space, key);
    value_escaped = curl_easy_escape(cli->curl, value, strlen(value));
    params = sdscatprintf(sdsempty(), "value=%s&prevValue=%s", value_escaped, prev);
    curl_free(value_escaped);
    if (ttl) {
        params = sdscatprintf(params, "&ttl=%lu", ttl);
    }
    req.data = params;
    resp = cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    sdsfree(params);
    return resp;
}
cetcd_response *cetcd_cmp_and_swap_by_index(cetcd_client *cli, const char *key, const char *value, uint64_t prev, uint64_t ttl) {
    cetcd_request req;
    cetcd_response *resp;
    cetcd_string params;
    char *value_escaped;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_PUT;
    req.api_type = ETCD_KEYS;
    req.uri = sdscatprintf(sdsempty(), "%s%s", cli->keys_space, key);
    value_escaped = curl_easy_escape(cli->curl, value, strlen(value));
    params = sdscatprintf(sdsempty(), "value=%s&prevIndex=%lu", value_escaped, prev);
    curl_free(value_escaped);
    if (ttl) {
        params = sdscatprintf(params, "&ttl=%lu", ttl);
    }
    req.data = params;
    resp = cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    sdsfree(params);
    return resp;
}
cetcd_response *cetcd_cmp_and_delete(cetcd_client *cli, const char *key, const char *prev) {
    cetcd_request req;
    cetcd_response *resp;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_DELETE;
    req.api_type = ETCD_KEYS;
    req.uri = sdscatprintf(sdsempty(), "%s%s?prevValue=%s", cli->keys_space, key, prev);
    resp = cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    return resp;
}
cetcd_response *cetcd_cmp_and_delete_by_index(cetcd_client *cli, const char *key, uint64_t prev) {
    cetcd_request req;
    cetcd_response *resp;

    memset(&req, 0, sizeof(cetcd_request));
    req.method = ETCD_HTTP_DELETE;
    req.api_type = ETCD_KEYS;
    req.uri = sdscatprintf(sdsempty(), "%s%s?prevIndex=%lu", cli->keys_space, key, prev);
    resp = cetcd_cluster_request(cli, &req);
    sdsfree(req.uri);
    return resp;
}
void cetcd_node_release(cetcd_response_node *node) {
    int i, count;
    cetcd_response_node *n;
    if (node->nodes) {
        count = cetcd_array_size(node->nodes);
        for (i = 0; i < count; ++i) {
            n = cetcd_array_get(node->nodes, i);
            cetcd_node_release(n);
        }
        cetcd_array_release(node->nodes);
    }
    if (node->key) {
        sdsfree(node->key);
    }
    if (node->value) {
        sdsfree(node->value);
    }
    free(node);
}
void cetcd_response_release(cetcd_response *resp) {
    if(resp) {
        if (resp->err) {
            cetcd_error_release(resp->err);
            resp->err = NULL;
        }
        if (resp->node) {
            cetcd_node_release(resp->node);
        }
        if (resp->prev_node) {
            cetcd_node_release(resp->prev_node);
        }
        free(resp);
    }
}
void cetcd_error_release(cetcd_error *err) {
    if (err) {
        if (err->message) {
            sdsfree(err->message);
        }
        if (err->cause) {
            sdsfree(err->cause);
        }
        free(err);
    }
}
static void cetcd_node_print(cetcd_response_node *node) {
    int i, count;
    cetcd_response_node *n;
    if (node) {
        printf("Node TTL: %lu\n", node->ttl);
        printf("Node ModifiedIndex: %lu\n", node->modified_index);
        printf("Node CreatedIndex: %lu\n", node->created_index);
        printf("Node Key: %s\n", node->key);
        printf("Node Value: %s\n", node->value);
        printf("Node Dir: %d\n", node->dir);
        printf("\n");
        if (node->nodes) {
            count = cetcd_array_size(node->nodes);
            for (i = 0; i < count; ++i) {
                n = cetcd_array_get(node->nodes, i);
                cetcd_node_print(n);
            }
        }
    }
}
void cetcd_response_print(cetcd_response *resp) {
    if (resp->err) {
        printf("Error Code:%d\n", resp->err->ecode);
        printf("Error Message:%s\n", resp->err->message);
        printf("Error Cause:%s\n", resp->err->cause);
        return;
    }
    printf("Etcd Action:%s\n", cetcd_event_action[resp->action]);
    printf("Etcd Index:%lu\n", resp->etcd_index);
    printf("Raft Index:%lu\n", resp->raft_index);
    printf("Raft Term:%lu\n", resp->raft_term);
    if (resp->node) {
        printf("-------------Node------------\n");
        cetcd_node_print(resp->node);
    }
    if (resp->prev_node) {
        printf("-----------prevNode------------\n");
        cetcd_node_print(resp->prev_node);
    }
}

size_t cetcd_parse_response(char *ptr, size_t size, size_t nmemb, void *userdata) {
    int len, i;
    char *key, *val;
    cetcd_response_parser *parser;
    yajl_status status;
    cetcd_response *resp = NULL;
    cetcd_array *addrs = NULL;

    enum resp_parser_st {
        request_line_start_st,
        request_line_end_st,
        request_line_http_status_start_st,
        request_line_http_status_st,
        request_line_http_status_end_st,
        header_key_start_st,
        header_key_st,
        header_key_end_st,
        header_val_start_st,
        header_val_st,
        header_val_end_st,
        blank_line_st,
        json_start_st,
        json_end_st,
        response_discard_st
    };
    /* Headers we are interested in:
     * X-Etcd-Index: 14695
     * X-Raft-Index: 672930
     * X-Raft-Term: 12
     * */
    parser = userdata;
    if (parser->api_type == ETCD_MEMBERS) {
        addrs = parser->resp;
    } else {
        resp = parser->resp;
    }
    len = size * nmemb;
    for (i = 0; i < len; ++i) {
        if (parser->st == request_line_start_st) {
            if (ptr[i] == ' ') {
                parser->st = request_line_http_status_start_st;
            }
            continue;
        }
        if (parser->st == request_line_end_st) {
            if (ptr[i] == '\n') {
                parser->st = header_key_start_st;
            }
            continue;
        }
        if (parser->st == request_line_http_status_start_st) {
            parser->buf = sdscatlen(parser->buf, ptr+i, 1);
            parser->st = request_line_http_status_st;
            continue;
        }
        if (parser->st == request_line_http_status_st) {
            if (ptr[i] == ' ') {
                parser->st = request_line_http_status_end_st;
            } else {
                parser->buf = sdscatlen(parser->buf, ptr+i, 1);
                continue;
            }
        }
        if (parser->st == request_line_http_status_end_st) {
            val = parser->buf;
            parser->http_status = atoi(val);
            sdsclear(parser->buf);
            parser->st = request_line_end_st;
            if (parser->api_type == ETCD_MEMBERS && parser->http_status != 200) {
                parser->st = response_discard_st;
            }
            continue;
        }
        if (parser->st == header_key_start_st) {
            if (ptr[i] == '\r') {
                ++i;
            }
            if (ptr[i] == '\n') {
                parser->st = blank_line_st;
                if (parser->http_status >= 300 && parser->http_status < 400) {
                    /*this is a redirection, restart the state machine*/
                    parser->st = request_line_start_st;
                    break;
                }
                continue;
            }
            parser->st = header_key_st;
        }
        if (parser->st == header_key_st) {
            parser->buf = sdscatlen(parser->buf, ptr+i, 1);
            if (ptr[i] == ':') {
                parser->st = header_key_end_st;
            } else {
                continue;
            }
        }
        if (parser->st == header_key_end_st) {
            parser->st = header_val_start_st;
            continue;
        }
        if (parser->st == header_val_start_st) {
            if (ptr[i] == ' ') {
                continue;
            }
            parser->st = header_val_st;
        }
        if (parser->st == header_val_st) {
            if (ptr[i] == '\r') {
                ++i;
            }
            if (ptr[i] == '\n') {
                parser->st = header_val_end_st;
            } else {
                parser->buf = sdscatlen(parser->buf, ptr+i, 1);
                continue;
            }
        }
        if (parser->st == header_val_end_st) {
            parser->st = header_key_start_st;
            if (parser->api_type == ETCD_MEMBERS) {
                sdsclear(parser->buf);
                continue;
            }
            int count = 0;
            sds *kvs = sdssplitlen(parser->buf, sdslen(parser->buf), ":", 1, &count);
            sdsclear(parser->buf);
            if (count < 2) {
                sdsfreesplitres(kvs, count);
                continue;
            }
            key = kvs[0];
            val = kvs[1];
            if (strncmp(key, "X-Etcd-Index", sizeof("X-Etcd-Index")-1) == 0) {
                resp->etcd_index = atoi(val);
            } else if (strncmp(key, "X-Raft-Index", sizeof("X-Raft-Index")-1) == 0) {
                resp->raft_index = atoi(val);
            } else if (strncmp(key, "X-Raft-Term", sizeof("X-Raft-Term")-1) == 0) {
                resp->raft_term = atoi(val);
            }
            sdsfreesplitres(kvs, count);
            continue;
        }
        if (parser->st == blank_line_st) {
            if (ptr[i] != '{') {
                /*not a json response, discard*/
                parser->st = response_discard_st;
                if (resp->err == NULL && parser->api_type == ETCD_KEYS) {
                    resp->err = calloc(1, sizeof(cetcd_error));
                    resp->err->ecode = error_response_parsed_failed;
                    resp->err->message = sdsnew("not a json response");
                    resp->err->cause = sdsnewlen(ptr, len);
                }
                continue;
            }
            parser->st = json_start_st;
            cetcd_array_init(&parser->ctx.keystack, 10);
            cetcd_array_init(&parser->ctx.nodestack, 10);
            if (parser->api_type == ETCD_MEMBERS) {
                parser->ctx.userdata = addrs;
                parser->json = yajl_alloc(&sync_callbacks, 0, &parser->ctx);
            }
            else {
                if (parser->http_status != 200 && parser->http_status != 201) {
                    resp->err = calloc(1, sizeof(cetcd_error));
                    parser->ctx.userdata = resp->err;
                    parser->json = yajl_alloc(&error_callbacks, 0, &parser->ctx);
                } else {
                    parser->ctx.userdata = resp;
                    parser->json = yajl_alloc(&callbacks, 0, &parser->ctx);
                }
            }
        }
        if (parser->st == json_start_st) {
            if (yajl_status_ok == yajl_parse(parser->json, (const unsigned char *)ptr + i, len - i)) {
                //all text left has been parsed, break the for loop
                break;
            } else {
                parser->st = json_end_st;
            }
        }
        if (parser->st == json_end_st) {
            status = yajl_complete_parse(parser->json);
            /*parse failed, TODO set error message*/
            if (status != yajl_status_ok) {
                if ( parser->api_type == ETCD_KEYS && resp->err == NULL) {
                    resp->err = calloc(1, sizeof(cetcd_error));
                    resp->err->ecode = error_response_parsed_failed;
                    resp->err->message = sdsnew("http response is invalid json format");
                    resp->err->cause = sdsnewlen(ptr, len);
                }
                return 0;
            }
            break;
        }
        if (parser->st == response_discard_st) {
            return len;
        }
    }
    return len;
}
void *cetcd_send_request(CURL *curl, cetcd_request *req) {
    CURLcode res;
    cetcd_response_parser parser;
    cetcd_response *resp = NULL ;
    cetcd_array *addrs = NULL;

    if (req->api_type == ETCD_MEMBERS) {
        addrs = cetcd_array_create(10);
        parser.resp = addrs;
    } else {
        resp = calloc(1, sizeof(cetcd_response));
        parser.resp = resp;
    }

    parser.api_type = req->api_type;
    parser.st = 0; /*0 should be the start state of the state machine*/
    parser.buf = sdsempty();
    parser.json = NULL;

    curl_easy_setopt(curl, CURLOPT_URL, req->url);
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, http_method[req->method]);
    if (req->method == ETCD_HTTP_PUT || req->method == ETCD_HTTP_POST) {
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, req->data);
    } else {
        /* We must clear post fields here:
         * We reuse the curl handle for all HTTP methods.
         * CURLOPT_POSTFIELDS would be set when issue a PUT request.
         * The field  pointed to the freed req->data. It would be
         * reused by next request.
         * */
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, "");
    }
    if (req->cli->settings.user) {
        curl_easy_setopt(curl, CURLOPT_USERNAME, req->cli->settings.user);
    }
    if (req->cli->settings.password) {
        curl_easy_setopt(curl, CURLOPT_PASSWORD, req->cli->settings.password);
    }
    curl_easy_setopt(curl, CURLOPT_HEADER, 1L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &parser);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cetcd_parse_response);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, req->cli->settings.verbose);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, req->cli->settings.connect_timeout);
    struct curl_slist *chunk = NULL;
    chunk = curl_slist_append(chunk, "Expect:");
    res = curl_easy_setopt(curl, CURLOPT_HTTPHEADER, chunk);

    res = curl_easy_perform(curl);

    curl_slist_free_all(chunk);
    //release the parser resource
    sdsfree(parser.buf);
    if (parser.json) {
        yajl_free(parser.json);
        cetcd_array_destroy(&parser.ctx.keystack);
        cetcd_array_destroy(&parser.ctx.nodestack);
    }

    if (res != CURLE_OK) {
        if (req->api_type == ETCD_MEMBERS) {
            return addrs;
        }
        if (resp->err == NULL) {
            resp->err = calloc(1, sizeof(cetcd_error));
            resp->err->ecode = error_send_request_failed;
            resp->err->message = sdsnew(curl_easy_strerror(res));
            resp->err->cause = sdsdup(req->url);
        }
        return resp;
    }
    return parser.resp;
}
/*
 * cetcd_cluster_request tries to request the whole cluster. It round-robin to next server if the request failed
 * */
void *cetcd_cluster_request(cetcd_client *cli, cetcd_request *req) {
    size_t i, count;
    cetcd_string url;
    cetcd_error *err = NULL;
    cetcd_response *resp = NULL;
    cetcd_array *addrs = NULL;
    void *res = NULL;

    count = cetcd_array_size(cli->addresses);

    for(i = 0; i < count; ++i) {
        url = sdscatprintf(sdsempty(), "%s/%s", (cetcd_string)cetcd_array_get(cli->addresses, cli->picked), req->uri);
        req->url = url;
        req->cli = cli;
        res = cetcd_send_request(cli->curl, req);
        sdsfree(url);

        if (req->api_type == ETCD_MEMBERS) {
            addrs = res;
            /* Got the result addresses, return*/
            if (addrs && cetcd_array_size(addrs)) {
                return addrs;
            }
            /* Empty or error ? retry */
            if (addrs) {
                cetcd_array_release(addrs);
                addrs = NULL;
            }
            if (i == count - 1) {
                break;
            }
        } else if (req->api_type == ETCD_KEYS) {
            resp = res;
            if(resp && resp->err && resp->err->ecode == error_send_request_failed) {
                if (i == count - 1) {
                    break;
                }
            cetcd_response_release(resp);
            } else {
                /*got response, return*/
                return resp;
            }

        }
        /*try next*/
        cli->picked = (cli->picked + 1) % count;
    }
    /*the whole cluster failed*/
    if (req->api_type == ETCD_MEMBERS) return NULL;
    if (resp) {
        if(resp->err) {
            err = resp->err; /*remember last error*/
        }
        resp->err = calloc(1, sizeof(cetcd_error));
        resp->err->ecode = error_cluster_failed;
        resp->err->message = sdsnew("etcd_cluster_request: all cluster servers failed.");
        if (err) {
            resp->err->message = sdscatprintf(resp->err->message, " last error: %s", err->message);
            cetcd_error_release(err);
        }
        resp->err->cause = sdsdup(req->uri);
    }
    return resp;
}
