#include <gflags/gflags.h>
#include <bthread/bthread.h>
#include <butil/logging.h>
#include <butil/string_printf.h>
#include <brpc/channel.h>
#include <brpc/couchbase.h>
#include <brpc/policy/couchbase_protocol.h>
#include <brpc/policy/couchbase_authenticator.h>

DEFINE_string(server, "127.0.0.1:11210", "IP Address of server");
DEFINE_string(connection_type, "single", "Connection type. Available values: single, pooled, short");
// DEFINE_string(username, "Administrator", "Couchbase username");
// DEFINE_string(password, "password", "Couchbase password");
// DEFINE_string(bucket_name, "testing", "Couchbase bucket name");
DEFINE_bool(use_bthread, true, "Use bthread to send requests");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 
DEFINE_bool(dont_fail, false, "Print fatal when some call failed");
DEFINE_int32(exptime, 0, "The to-be-got data will be expired after so many seconds");

uint32_t batch_size = 10;

bvar::LatencyRecorder g_latency_recorder("client");
bvar::Adder<int> g_error_count("client_error_count");
butil::static_atomic<int> g_sender_count = BUTIL_STATIC_ATOMIC_INIT(0);

static void* sender(void* arg) {
    google::protobuf::RpcChannel* channel = 
        static_cast<google::protobuf::RpcChannel*>(arg);
    const int base_index = g_sender_count.fetch_add(1, butil::memory_order_relaxed);

    std::string value;
    std::string key = "test_brpc_";
 // Example batch size, can be parameterized

    brpc::CouchbaseRequest request;
    for (int i = 0; i < batch_size; ++i) {
        CHECK(request.Get(butil::string_printf("%s%d", key.c_str(), i)));
    }
    while (!brpc::IsAskedToQuit()) {
        // We will receive response synchronously, safe to put variables
        // on stack.
        brpc::CouchbaseResponse response;
        brpc::Controller cntl;

        // Because `done'(last parameter) is NULL, this function waits until
        // the response comes back or error occurs(including timedout).
        channel->CallMethod(NULL, &cntl, &request, &response, NULL);
        const int64_t elp = cntl.latency_us();
        if (!cntl.Failed()) {
            int i = 0;
            g_latency_recorder << cntl.latency_us();
            for (i = 0; i < batch_size; ++i) {
                uint32_t flags;
                if (!response.PopGet(&value, &flags, NULL)) {
                    LOG(INFO) << "Fail to GET the key, " << response.LastError();
                    std::cout<<"thread id "<< bthread_self() << " failed to get key: " << butil::string_printf("%s%d", key.c_str(), i) << std::endl;
                    break;
                }
                std::cout<<"thread id "<< bthread_self() <<"Key: " << butil::string_printf("%s%d", key.c_str(), i) << ", Value: " << value << std::endl;
            }
            bthread_usleep(5000000); // Sleep for 50ms before exiting
            return NULL;
            
        } else {
            g_error_count << 1; 
            bthread_usleep(5000000);
            return NULL;
        }
    }
    return NULL;
}

int main(){
    brpc::Channel channel;
    
    // Initialize the channel, NULL means using default options. 
    brpc::ChannelOptions options;
    options.protocol = brpc::PROTOCOL_COUCHBASE;
    options.connection_type = FLAGS_connection_type;
    options.timeout_ms = 2000 /*milliseconds*/;
    options.max_retry = 2;

    if (channel.Init(FLAGS_server.c_str(), FLAGS_load_balancer.c_str(), &options) != 0) {
        LOG(ERROR) << "Fail to initialize channel";
        return -1;
    }
    std::cout<<"Channel initialized successfully"<<std::endl;
    // Couchbase Authentication packet(SASL Auth) is now present in the channel 
    // now a request can be sent with which auth packet will also be sent.
    
    
    //create authrequest and authresponse
    brpc::Controller cntl;
    brpc::CouchbaseRequest auth_request;
    brpc::CouchbaseResponse auth_response;
    
    // Ask username and password for authentication
    std::string username;
    std::string password;
    while(username.empty() || password.empty()) {
        std::cout << "Enter Couchbase username: ";
        std::cin >> username;
        if(username.empty()) {
            std::cout << "Username cannot be empty. Please enter again." << std::endl;
            continue;
        }
        std::cout << "Enter Couchbase password: ";
        std::cin >> password;
        if(password.empty()) {
            std::cout << "Password cannot be empty. Please enter again." << std::endl;
            continue;
        }
    }

    if(!auth_request.Authenticate(username.c_str(), password.c_str())) {
        LOG(ERROR) << "Fail to create authentication request";
        return -1;
    }
    channel.CallMethod(NULL, &cntl, &auth_request, &auth_response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    cntl.Reset();

    std::cout << "Authentication successful, proceeding with Couchbase operations..." << std::endl;

    // Select bucket
    std::string bucket_name;
    while(bucket_name.empty()) {
        std::cout << "Enter Couchbase bucket name: ";
        std::cin >> bucket_name;
        if(bucket_name.empty()) {
            std::cout << "Bucket name cannot be empty. Please enter again." << std::endl;
            continue;
        }
    }
    brpc::CouchbaseRequest select_bucket_request;
    brpc::CouchbaseResponse select_bucket_response;
    if (!select_bucket_request.SelectBucket(bucket_name.c_str())) {
        LOG(ERROR) << "Fail to SELECT bucket request";
        return -1;
    }
    channel.CallMethod(NULL, &cntl, &select_bucket_request, &select_bucket_response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    cntl.Reset();

    if (select_bucket_response.LastError().empty()) {
        std::cout << "Bucket selected successfully: " << bucket_name << std::endl;
    } else {
        std::cout << "Failed to select bucket: " << select_bucket_response.LastError() << std::endl;
        return -1;
    }

    //enter batch size 
    std::cout << "Enter batch size for operations (e.g., 10): ";
    std::cin >> batch_size;

    // Add operation
    brpc::CouchbaseRequest add_request;
    brpc::CouchbaseResponse add_response;

    for(int i = 0;i<batch_size;i++) {
        if (!add_request.Add(butil::string_printf("test_brpc_%d", i), "{\"hello\":\"world\"}", 0xdeadbeef + i, FLAGS_exptime, 0)) {
            LOG(ERROR) << "Fail to ADD request";
            return -1;
        }
    }
    channel.CallMethod(NULL, &cntl, &add_request, &add_response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }

    for (int i = 0; i < batch_size; ++i) {
        if (!add_response.PopAdd(NULL)) {
            LOG(ERROR) << "Fail to ADD memcache, i=" << i
                       << ", " << add_response.LastError();
        }
    }
    cntl.Reset();

    std::vector<bthread_t> bids;
    std::vector<pthread_t> pids;
    if (!FLAGS_use_bthread) {
        pids.resize( batch_size);
        for (int i = 0; i <  batch_size; ++i) {
            if (pthread_create(&pids[i], NULL, sender, &channel) != 0) {
                LOG(ERROR) << "Fail to create pthread";
                return -1;
            }
        }
    } else {
        bids.resize( batch_size);
        for (int i = 0; i <  batch_size; ++i) {
            if (bthread_start_background(
                    &bids[i], NULL, sender, &channel) != 0) {
                LOG(ERROR) << "Fail to create bthread";
                return -1;
            }
        }
    }

    LOG(INFO) << "couchbase_client is going to quit";
    for (int i = 0; i <  batch_size; ++i) {
        if (!FLAGS_use_bthread) {
            pthread_join(pids[i], NULL);
        } else {
            bthread_join(bids[i], NULL);
        }
    }
    // // Get operation
    // brpc::CouchbaseRequest get_request;
    // brpc::CouchbaseResponse get_response;
    // if (!get_request.Get(butil::string_printf("%s", "test_brpc"))) {
    //     LOG(ERROR) << "Fail to GET request";
    //     return -1;
    // }
    // channel.CallMethod(NULL, &cntl, &get_request, &get_response, NULL);
    // if (cntl.Failed()) {
    //     LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
    //     return -1;
    // }
    // std::string value;
    // uint32_t flags = 0;
    // uint64_t cas = 0;
    // if (get_response.PopGet(&value, &flags, &cas)) {
    //     std::cout << "GET value: " << value << std::endl;
    //     std::cout << "Flags: " << flags << std::endl;
    //     std::cout << "CAS: " << cas << std::endl;
    // } else {
    //     std::cout << "Raw response (hex): ";
    //     for (char c : get_response.raw_buffer().to_string()) {
    //         printf("%02x ", static_cast<unsigned char>(c));
    //     }
    //     std::cout << std::endl;
    // }
    return 0;
}




