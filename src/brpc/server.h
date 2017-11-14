// Copyright (c) 2014 Baidu, Inc.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// Authors: Ge,Jun (gejun@baidu.com)
//          Rujie Jiang (jiangrujie@baidu.com)

#ifndef BRPC_SERVER_H
#define BRPC_SERVER_H

// To brpc developers: This is a header included by user, don't depend
// on internal structures, use opaque pointers instead.

#include "bthread/errno.h"        // Redefine errno
#include "bthread/bthread.h"      // Server may need some bthread functions,
                                  // e.g. bthread_usleep
#include <google/protobuf/service.h>                // google::protobuf::Service
#include "butil/macros.h"                            // DISALLOW_COPY_AND_ASSIGN
#include "butil/containers/doubly_buffered_data.h"   // DoublyBufferedData
#include "bvar/bvar.h"
#include "butil/containers/case_ignored_flat_map.h"  // [CaseIgnored]FlatMap
#include "brpc/controller.h"                   // brpc::Controller
#include "brpc/describable.h"                  // User often needs this
#include "brpc/data_factory.h"                 // DataFactory
#include "brpc/builtin/tabbed.h"
#include "brpc/details/profiler_linker.h"
#include "brpc/health_reporter.h"

extern "C" {
struct ssl_ctx_st;
}

namespace brpc {

class Acceptor;
class MethodStatus;
class NsheadService;
class SimpleDataPool;
class MongoServiceAdaptor;
class RestfulMap;
class RtmpService;

struct CertInfo {
    // Certificate in PEM format.
    // Note that CN and alt subjects will be extracted from the certificate,
    // and will be used as hostnames. Requests to this hostname (provided SNI
    // extension supported) will be encrypted using this certifcate. 
    // Supported both file path and raw string
    std::string certificate;

    // Private key in PEM format.
    // Supported both file path and raw string based on prefix:
    std::string private_key;
        
    // Additional hostnames besides those inside the certificate. Wildcards
    // are supported but it can only appear once at the beginning (i.e. *.xxx.com).
    std::vector<std::string> sni_filters;
};

struct SSLOptions {
    // Constructed with default options
    SSLOptions();

    // Default certificate which will be loaded into server. Requests
    // without hostname or whose hostname doesn't have a corresponding
    // certificate will use this certificate. MUST be set to enable SSL.
    CertInfo default_cert;
    
    // Additional certificates which will be loaded into server. These
    // provide extra bindings between hostnames and certificates so that
    // we can choose different certificates according to different hostnames.
    // See `CertInfo' for detail.
    std::vector<CertInfo> certs;

    // When set, requests without hostname or whose hostname can't be found in
    // any of the cerficates above will be dropped. Otherwise, `default_cert'
    // will be used.
    // Default: false
    bool strict_sni;

    // When set, SSLv3 requests will be dropped. Strongly recommended since
    // SSLv3 has been found suffering from severe security problems. Note that
    // some old versions of browsers may use SSLv3 by default such as IE6.0
    // Default: true
    bool disable_ssl3;

    // Maximum lifetime for a session to be cached inside OpenSSL in seconds.
    // A session can be reused (initiated by client) to save handshake before
    // it reaches this timeout.
    // Default: 300
    int session_lifetime_s;

    // Maximum number of cached sessions. When cache is full, no more new
    // session will be added into the cache until SSL_CTX_flush_sessions is
    // called (automatically by SSL_read/write). A special value is 0, which
    // means no limit.
    // Default: 20480
    int session_cache_size;

    // Cipher suites allowed for each SSL handshake. The format of this string
    // should follow that in `man 1 cipers'. If empty, OpenSSL will choose
    // a default cipher based on the certificate information
    // Default: ""
    std::string ciphers;

    // Name of the elliptic curve used to generate ECDH ephemerial keys
    // Default: prime256v1
    std::string ecdhe_curve_name;
    
    // TODO: Support NPN & ALPN
    // TODO: Support OSCP stapling
};

struct ServerOptions {
    // Constructed with default options.
    ServerOptions();
        
    // connections without data transmission for so many seconds will be closed
    // Default: -1 (disabled)
    int idle_timeout_sec;

    // If this option is not empty, a file named so containing Process Id
    // of the server will be created when the server is started.
    // Default: ""
    std::string pid_file;
    
    // Process requests in format of nshead_t + blob.
    // Owned by Server and deleted in server's destructor
    // Default: NULL
    NsheadService* nshead_service;

    // Adaptor for Mongo protocol, check src/brpc/mongo_service_adaptor.h for details
    // The adaptor will not be deleted by server
    // and must remain valid when server is running.
    const MongoServiceAdaptor* mongo_service_adaptor;

    // Turn on authentication for all services if `auth' is not NULL.
    // Default: NULL
    const Authenticator* auth;

    // false: `auth' is not owned by server and must be valid when server is running.
    // true:  `auth' is owned by server and will be deleted when server is destructed.
    // Default: false
    bool server_owns_auth;

    // Number of pthreads that server runs on. Notice that this is just a hint,
    // you can't assume that the server uses exactly so many pthreads because
    // pthread workers are shared by all servers and channels inside a 
    // process. And there're no "io-thread" and "worker-thread" anymore,
    // brpc automatically schedules "io" and "worker" code for better
    // parallelism and less context switches.
    // If this option <= 0, number of pthread workers is not changed.
    // Default: #cpu-cores
    int num_threads;

    // Limit number of requests processed in parallel. To limit the max
    // concurrency of a method, use server.MaxConcurrencyOf("xxx") instead.
    //
    // In a traditional server, number of pthread workers also limits
    // concurrency. However brpc runs requests in bthreads which are
    // mapped to pthread workers, when a bthread context switches, it gives
    // the pthread worker to another bthread, yielding a higher concurrency
    // than number of pthreads. In some situation, higher concurrency may
    // consume more resources, to protect the server from running out of
    // resources, you may set this option.
    // If the server reaches the limitation, it responds client with ELIMIT
    // directly without calling service's callback. The client seeing ELIMIT
    // shall try another server.
    // NOTE: accesses to builtin services are not limited by this option.
    // Default: 0 (unlimited)
    int max_concurrency;

    // -------------------------------------------------------
    // Differences between session-local and thread-local data
    // -------------------------------------------------------
    // * session-local data has to be got from a server-side Controller.
    //   thread-local data can be got in any function running inside a server
    //   thread without an object.
    // * session-local data is attached to current RPC and invalid after 
    //   calling `done'. thread-local data is attached to current server
    //   thread and invalid after leaving Service::CallMethod(). If you need
    //   to hand down data to an asynchronous `done' which is called outside
    //   Service::CallMethod(), you must use session-local data.
    // -----------------
    // General guideline
    // -----------------
    // * Choose a proper value for reserved_xxx_local_data, small value may 
    //   not create enough data for all searching threads, while big value 
    //   wastes memory.
    // * Comparing to reserving data, making them as small as possible and
    //   creating them on demand is better. Passing a lot of stuff via
    //   session-local data or thread-local data is definitely not a good design.
    
    // The factory to create/destroy data attached to each RPC session.
    // If this option is NULL, Controller::session_local_data() is always NULL.
    // NOT owned by Server and must be valid when Server is running.
    // Default: NULL
    const DataFactory* session_local_data_factory;

    // Prepare so many session-local data before server starts, so that calls
    // to Controller::session_local_data() get data directly rather than 
    // calling session_local_data_factory->Create() at first time. Useful when
    // Create() is slow, otherwise the RPC session may be blocked by the
    // creation of data and not served within timeout.
    // Default: 0
    size_t reserved_session_local_data;

    // The factory to create/destroy data attached to each searching thread
    // in server.
    // If this option is NULL, brpc::thread_local_data() is always NULL.
    // NOT owned by Server and must be valid when Server is running.
    // Default: NULL
    const DataFactory* thread_local_data_factory;

    // Prepare so many thread-local data before server starts, so that calls
    // to brpc::thread_local_data() get data directly rather than calling
    // thread_local_data_factory->Create() at first time. Useful when Create()
    // is slow, otherwise the RPC session may be blocked by the creation
    // of data and not served within timeout.
    // Default: 0
    size_t reserved_thread_local_data;

    // Call bthread_init_fn(bthread_init_args) in at least #bthread_init_count
    // bthreads before server runs, mainly for initializing bthread locals.
    // You have to set both `bthread_init_fn' and `bthread_init_count' to
    // enable the feature. 
    bool (*bthread_init_fn)(void* args); // default: NULL (do nothing)
    void* bthread_init_args;             // default: NULL
    size_t bthread_init_count;           // default: 0

    // Provide builtin services at this port rather than the port to Start().
    // When your server needs to be accessed from public (including traffic
    // redirected by nginx or other http front-end servers), set this port
    // to a port number that's ONLY accessible from internal network
    // so that you can check out the builtin services from this port while
    // hiding them from public. Setting this option also enables security
    // protection code which we may add constantly.
    // Update: this option affects Tabbed services as well.
    // Default: -1
    int internal_port;

    // Contain a set of builtin services to ease monitoring/debugging.
    // Read docs/cn/builtin_service.md for details.
    // DO NOT set this option to false if you don't even know what builtin
    // services are. They're very helpful for addressing runtime problems.
    // Setting to false makes -internal_port ineffective.
    // Default: true
    bool has_builtin_services;

    // Enable more secured code which protects internal information from exposure.
    bool security_mode() const { return internal_port >= 0 || !has_builtin_services; }

    // SSL related options. Refer to `SSLOptions' for details
    SSLOptions ssl_options;
    
    // [CAUTION] This option is for implementing specialized http proxies,
    // most users don't need it. Don't change this option unless you fully
    // understand the description below.
    // If this option is set, all HTTP requests to the server will be delegated
    // to this service which fully decides what to call and what to send back,
    // including accesses to builtin services and pb services.
    // The service must have a method named "default_method" and the request
    // and response must have no fields.
    // This service is owned by server and deleted in server's destructor
    google::protobuf::Service* http_master_service;

    // If this field is on, contents on /health page is generated by calling
    // health_reporter->GenerateReport(). This object is NOT owned by server
    // and must remain valid when server is running.
    HealthReporter* health_reporter;

    // For processing RTMP connections. Read src/brpc/rtmp.h for details.
    // Default: NULL (rtmp support disabled)
    RtmpService* rtmp_service;

    // Only enable these protocols, separated by spaces.
    // All names inside must be valid, check protocols name in global.cpp
    // Default: empty (all protocols)
    std::string enabled_protocols;
};

// This struct is originally designed to contain basic statistics of the
// server. But bvar contains more stats and is more convenient.
struct ServerStatistics {
    size_t connection_count;
    int user_service_count;
    int builtin_service_count;
};

// Represent server's ownership of services.
enum ServiceOwnership {
    SERVER_OWNS_SERVICE,
    SERVER_DOESNT_OWN_SERVICE
};

struct ServiceOptions {
    ServiceOptions(); // constructed with default options.

    // SERVER_OWNS_SERVICE: the service will be deleted by the server.
    // SERVER_DOESNT_OWN_SERVICE: the service shall be deleted by user after
    // stopping the server.
    // Default: SERVER_DOESNT_OWN_SERVICE
    ServiceOwnership ownership;
    
    // If this option is non-empty, methods in the service will be exposed
    // on specified paths instead of default "/SERVICE/METHOD".
    // Mappings are in form of: "PATH1 => NAME1, PATH2 => NAME2 ..." where
    // PATHs are valid http paths, NAMEs are method names in the service.
    // Default: empty
    std::string restful_mappings;

    // [ Not recommended to change this option ]
    // If this flag is true, the service will convert http body to protobuf
    // when the pb schema is non-empty in http servings. The body must be
    // valid json or protobuf(wire-format) otherwise the request is rejected.
    // This option does not affect pure-http services (pb schema is empty).
    // Services that use older versions of brpc may need to turn this
    // conversion off and handle http requests by their own to keep compatible
    // with existing clients.
    // Default: true
    bool allow_http_body_to_pb;

    // decode json string to protobuf bytes using base64 decoding when this 
    // option is turned on.
    // Default: false if BAIDU_INTERNAL is defined, otherwise true
    bool pb_bytes_to_base64;
};

// Represent ports inside [min_port, max_port]
struct PortRange {
    int min_port;
    int max_port;

    PortRange(int min_port2, int max_port2)
        : min_port(min_port2), max_port(max_port2) {
    }
};

// Server dispatches requests from clients to registered services and
// and sends responses back to clients.
class Server {
public:
    enum Status {
        UNINITIALIZED = 0,
        READY = 1,
        RUNNING = 2,
        STOPPING = 3,
    };
    struct ServiceProperty {
        bool is_builtin_service;
        ServiceOwnership ownership;
        // `service' and `restful_map' are mutual exclusive, they can't be
        // both non-NULL. If `restful_map' is not NULL, the URL should be
        // further matched by it.
        google::protobuf::Service* service;
        RestfulMap* restful_map;

        bool is_user_service() const {
            return !is_builtin_service && !restful_map;
        }

        const std::string& service_name() const;
    };
    typedef butil::FlatMap<std::string, ServiceProperty> ServiceMap;

    struct MethodProperty {
        bool is_builtin_service;
        bool own_method_status;
        // Parameters which have nothing to do with management of services, but
        // will be used when the service is queried.
        struct OpaqueParams {
            bool is_tabbed;
            bool allow_http_body_to_pb;
            bool pb_bytes_to_base64;
            OpaqueParams();
        };
        OpaqueParams params;        
        // NULL if service of the method was never added as restful.
        // "@path1 @path2 ..." if the method was mapped from paths.
        std::string* http_url;
        google::protobuf::Service* service;
        const google::protobuf::MethodDescriptor* method;
        MethodStatus* status;

        MethodProperty();
    };
    typedef butil::FlatMap<std::string, MethodProperty> MethodMap;

    struct ThreadLocalOptions {
        bthread_key_t tls_key;
        const DataFactory* thread_local_data_factory;
        
        ThreadLocalOptions()
            : tls_key(INVALID_BTHREAD_KEY)
            , thread_local_data_factory(NULL) {}
    };

public:
    Server(ProfilerLinker = ProfilerLinker());
    ~Server();

    // A set of functions to start this server.
    // Returns 0 on success, -1 otherwise and errno is set appropriately.
    // Notes:
    // * Default options are taken if `opt' is NULL.
    // * A server can be started more than once if the server is completely
    //   stopped by Stop() and Join().
    // * port can be 0, which makes kernel to choose a port dynamically.
    
    // Start on an address in form of "0.0.0.0:8000".
    int Start(const char* ip_port_str, const ServerOptions* opt);
    int Start(const butil::EndPoint& ip_port, const ServerOptions* opt);
    // Start on IP_ANY:port.
    int Start(int port, const ServerOptions* opt);
    // Start on `ip_str' + any useable port in `range'
    int Start(const char* ip_str, PortRange range, const ServerOptions *opt);

    // NOTE: Stop() is paired with Join() to stop a server without losing
    // requests. The point of separating them is that you can Stop() multiple
    // servers before Join() them, in which case the total time to Join is
    // time of the slowest Join(). Otherwise you have to Join() them one by
    // one, in which case the total time is sum of all Join().

    // Stop accepting new connections and requests from existing connections.
    // Returns 0 on success, -1 otherwise.
    int Stop(int closewait_ms/*not used anymore*/);

    // Wait until requests in progress are done. If Stop() is not called,
    // this function NEVER return. If Stop() is called, during the waiting, 
    // this server responds new requests with `ELOGOFF' error immediately 
    // without calling any service. When clients see the error, they should 
    // try other servers.
    int Join();

    // Sleep until Ctrl-C is pressed, then stop and join this server.
    // CAUTION: Don't call signal(SIGINT, ...) in your program!
    // If signal(SIGINT, ..) is called AFTER calling this function, this 
    // function may block indefinitely.
    void RunUntilAskedToQuit();

    // Add a service. Arguments are explained in ServiceOptions above.
    // NOTE: Adding a service while server is running is forbidden.
    // Returns 0 on success, -1 otherwise.
    int AddService(google::protobuf::Service* service,
                   ServiceOwnership ownership);
    int AddService(google::protobuf::Service* service,
                   ServiceOwnership ownership,
                   const butil::StringPiece& restful_mappings);
    int AddService(google::protobuf::Service* service,
                   const ServiceOptions& options);

    // Remove a service from this server.
    // NOTE: removing a service while server is running is forbidden.
    // Returns 0 on success, -1 otherwise.
    int RemoveService(google::protobuf::Service* service);
    
    // Remove all services from this server.
    // NOTE: clearing services when server is running is forbidden.
    void ClearServices();

    // Dynamically add a new certificate into server. It can be called
    // while the server is running, but it's not thread-safe by itself.
    // Returns 0 on success, -1 otherwise.
    int AddCertificate(const CertInfo& cert);

    // Dynamically remove a former certificate from server. Can be called
    // while the server is running, but it's not thread-safe by itself.
    // Returns 0 on success, -1 otherwise.
    int RemoveCertificate(const CertInfo& cert);

    // Dynamically reset all certificates except the default one. It can be
    // called while the server is running, but it's not thread-safe by itself.
    // Returns 0 on success, -1 otherwise.
    int ResetCertificates(const std::vector<CertInfo>& certs);

    // Find a service by its ServiceDescriptor::full_name().
    // Returns the registered service pointer, NULL on not found.
    // Notice that for performance concerns, this function does not lock service
    // list internally thus races with AddService()/RemoveService().
    google::protobuf::Service*
    FindServiceByFullName(const butil::StringPiece& full_name) const;

    // Find a service by its ServiceDescriptor::name().
    // Returns the registered service pointer, NULL on not found.
    // Notice that for performance concerns, this function does not lock service
    // list internally thus races with AddService()/RemoveService().
    google::protobuf::Service*
    FindServiceByName(const butil::StringPiece& name) const;

    // Put all services registered by user into `services'
    void ListServices(std::vector<google::protobuf::Service*>* services);

    // Get statistics of this server
    void GetStat(ServerStatistics* stat) const;
    
    // Get the options passed to Start().
    const ServerOptions& options() const { return _options; }

    // Status of this server.
    Status status() const { return _status; }

    // Return true iff this server is serving requests.
    bool IsRunning() const { return status() == RUNNING; }

    // Return the first service added to this server. If a service was once
    // returned by first_service() and then removed, first_service() will
    // always be NULL.
    // This is useful for some production lines whose protocol does not 
    // contain a service name, in which case this service works as the 
    // default service.
    google::protobuf::Service* first_service() const
    { return _first_service; }

    // Set version string for this server, will be shown in /version page
    void set_version(const std::string& version) { _version = version; }
    const std::string& version() const { return _version; }

    // Return the address this server is listening
    butil::EndPoint listen_address() const { return _listen_addr; }
    
    // Last time that Start() was successfully called. 0 if Start() was
    // never called
    time_t last_start_time() const { return _last_start_time; }

    SimpleDataPool* session_local_data_pool() const { return _session_local_data_pool; }

    const ThreadLocalOptions& thread_local_options() const { return _tl_options; }

    // Print the html code of tabs into `os'.
    // current_tab_name is the tab highlighted.
    void PrintTabsBody(std::ostream& os, const char* current_tab_name) const;

    // Reset the max_concurrency set by ServerOptions.max_concurrency after
    // Server is started.
    // The concurrency will be limited by the new value if this function is
    // successfully returned.
    // Returns 0 on success, -1 otherwise.
    int ResetMaxConcurrency(int max_concurrency);

    // Get/set max_concurrency associated with a method.
    // Example:
    //    server.MaxConcurrencyOf("example.EchoService.Echo") = 10;
    // or server.MaxConcurrencyOf("example.EchoService", "Echo") = 10;
    // or server.MaxConcurrencyOf(&service, "Echo") = 10;
    int& MaxConcurrencyOf(const butil::StringPiece& full_method_name);
    int MaxConcurrencyOf(const butil::StringPiece& full_method_name) const;
    
    int& MaxConcurrencyOf(const butil::StringPiece& full_service_name,
                          const butil::StringPiece& method_name);
    int MaxConcurrencyOf(const butil::StringPiece& full_service_name,
                         const butil::StringPiece& method_name) const;

    int& MaxConcurrencyOf(google::protobuf::Service* service,
                          const butil::StringPiece& method_name);
    int MaxConcurrencyOf(google::protobuf::Service* service,
                         const butil::StringPiece& method_name) const;

private:
friend class StatusService;
friend class ProtobufsService;
friend class ConnectionsService;
friend class BadMethodService;
friend class ServerPrivateAccessor;
friend class Controller;

    int AddServiceInternal(google::protobuf::Service* service,
                           bool is_builtin_service,
                           const ServiceOptions& options);

    int AddBuiltinService(google::protobuf::Service* service);

    // Remove all methods of `service' from internal structures.
    void RemoveMethodsOf(google::protobuf::Service* service);

    int AddBuiltinServices();

    // Initialize internal structure. Initializtion is
    // ensured to be called only once
    int InitializeOnce();

    // Create acceptor with handlers of protocols.
    Acceptor* BuildAcceptor();

    int StartInternal(const butil::ip_t& ip,
                      const PortRange& port_range,
                      const ServerOptions *opt);

    // Number of user added services, not counting builtin services.
    size_t service_count() const {
        return _fullname_service_map.size() -
            _builtin_service_count -
            _virtual_service_count;
    }

    // Number of builtin services.
    size_t builtin_service_count() const { return _builtin_service_count; }

    static void* UpdateDerivedVars(void*);

    void GenerateVersionIfNeeded();
    void PutPidFileIfNeeded();

    const MethodProperty*
    FindMethodPropertyByFullName(const butil::StringPiece& fullname) const;

    const MethodProperty*
    FindMethodPropertyByFullName(const butil::StringPiece& full_service_name,
                                 const butil::StringPiece& method_name) const;

    const MethodProperty*
    FindMethodPropertyByNameAndIndex(const butil::StringPiece& service_name,
                                     int method_index) const;
    
    const ServiceProperty*
    FindServicePropertyByFullName(const butil::StringPiece& fullname) const;

    const ServiceProperty*
    FindServicePropertyByName(const butil::StringPiece& name) const;
    
    std::string ServerPrefix() const;

    // Mapping from hostname to corresponding SSL_CTX
    typedef butil::CaseIgnoredFlatMap<struct ssl_ctx_st*> CertMap;
    struct CertMaps {
        CertMap cert_map;
        CertMap wildcard_cert_map;
    };

    struct SSLContext {
        struct ssl_ctx_st* ctx;
        std::vector<std::string> filters;
    };
    // Mapping from [certficate + private-key] to SSLContext
    typedef butil::FlatMap<std::string, SSLContext> SSLContextMap;

    void FreeSSLContexts();
    void FreeSSLContextMap(SSLContextMap& ctx_map, bool keep_default);

    static int SSLSwitchCTXByHostname(struct ssl_st* ssl,
                                      int* al, Server* server);

    static bool AddCertMapping(CertMaps& bg, const SSLContext& ssl_ctx);
    static bool RemoveCertMapping(CertMaps& bg, const SSLContext& ssl_ctx);
    static bool ResetCertMappings(CertMaps& bg, const SSLContextMap& ctx_map);
    static bool ClearCertMapping(CertMaps& bg);

    int& MaxConcurrencyOf(MethodProperty*);
    int MaxConcurrencyOf(const MethodProperty*) const;
    
    DISALLOW_COPY_AND_ASSIGN(Server);

    // Put frequently-accessed data pool at first.
    SimpleDataPool* _session_local_data_pool;
    ThreadLocalOptions _tl_options;
    
    Status _status;
    int _builtin_service_count;
    // number of the virtual services for mapping URL to methods.
    int _virtual_service_count;
    bool _failed_to_set_max_concurrency_of_method;
    Acceptor* _am;
    Acceptor* _internal_am;
    
    // Use method->full_name() as key
    MethodMap _method_map;

    // Use service->full_name() as key
    ServiceMap _fullname_service_map;
    
    // In order to be compatible with some RPC framework that
    // uses service->name() to designate an RPC service
    ServiceMap _service_map;

    // The only non-builtin service in _service_map, otherwise NULL.
    google::protobuf::Service* _first_service;

    // Store TabInfo of services inheriting Tabbed.
    TabInfoList* _tab_info_list;

    // Store url patterns for paths without exact service names, examples:
    //   *.flv => Method
    //   abc*  => Method
    RestfulMap* _global_restful_map;

    // Default certficate which can't be reloaded
    struct ssl_ctx_st* _default_ssl_ctx;

    // Reloadable SSL mappings
    butil::DoublyBufferedData<CertMaps> _reload_cert_maps;

    // Holds the memory of all SSL_CTXs
    SSLContextMap _ssl_ctx_map;
    
    ServerOptions _options;
    butil::EndPoint _listen_addr;

    std::string _version;
    time_t _last_start_time;
    bthread_t _derivative_thread;
    
    bthread_keytable_pool_t* _keytable_pool;

    // FIXME: Temporarily for `ServerPrivateAccessor' to change this bvar
    //        Replace `ServerPrivateAccessor' with other private-access
    //        mechanism
    mutable bvar::Adder<int64_t> _nerror;
    mutable int32_t BAIDU_CACHELINE_ALIGNMENT _concurrency;
};

// Get the data attached to current searching thread. The data is created by
// ServerOptions.thread_local_data_factory and reused between different threads.
// If ServerOptions.thread_local_data_factory is NULL, return NULL.
// If this function is not called inside a server thread, return NULL.
void* thread_local_data();

// Test if a dummy server was already started.
bool IsDummyServerRunning();

// Start a dummy server listening at `port'. If a dummy server was already
// running, this function does nothing and fails.
// NOTE: The second parameter(ProfilerLinker) is for linking of profiling 
// functions when corresponding macros are defined, just ignore it.
// Returns 0 on success, -1 otherwise.
int StartDummyServerAt(int port, ProfilerLinker = ProfilerLinker());

} // namespace brpc

#endif  // BRPC_SERVER_H
