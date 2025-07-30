#include <gflags/gflags.h>
#include <bthread/bthread.h>
#include <butil/logging.h>
#include <butil/string_printf.h>
#include <brpc/channel.h>
#include <brpc/couchbase.h>
#include <brpc/policy/couchbase_protocol.h>
#include <brpc/policy/couchbase_authenticator.h>
DEFINE_string(server, "127.0.0.1:11210", "IP Address of server");
DEFINE_string(connection_type, "pooled", "Connection type. Available values: single, pooled, short");
DEFINE_string(username, "Administrator", "Couchbase username");
DEFINE_string(password, "password", "Couchbase password");
DEFINE_string(bucket_name, "testing", "Couchbase bucket name");
DEFINE_string(load_balancer, "", "The algorithm for load balancing");
DEFINE_int32(max_retry, 3, "Max retries(not including the first RPC)"); 
DEFINE_bool(dont_fail, false, "Print fatal when some call failed");
DEFINE_int32(exptime, 0, "The to-be-got data will be expired after so many seconds");
bvar::LatencyRecorder g_latency_recorder("client");
bvar::Adder<int> g_error_count("client_error_count");
butil::static_atomic<int> g_sender_count = BUTIL_STATIC_ATOMIC_INIT(0);
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
    if(!auth_request.Authenticate(FLAGS_username.c_str(), FLAGS_password.c_str())) {
        LOG(ERROR) << "Fail to create authentication request";
        return -1;
    }
    channel.CallMethod(NULL, &cntl, &auth_request, &auth_response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    cntl.Reset();
    brpc::CouchbaseRequest select_bucket_request;
    brpc::CouchbaseResponse select_bucket_response;

    if (!select_bucket_request.SelectBucket(FLAGS_bucket_name.c_str())) {
        LOG(ERROR) << "Fail to SELECT bucket request";
        return -1;
    }
    channel.CallMethod(NULL, &cntl, &select_bucket_request, &select_bucket_response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    cntl.Reset();
    //print the content of the select_bucket_response variable
    std::cout << "Select Bucket Response: " << select_bucket_response.raw_buffer().to_string() << std::endl;
    cntl.Reset();
    if (!select_bucket_request.Add("document-key3",
        "{\"hello\":\"world\"}", // <-- JSON string
        0, FLAGS_exptime, 0)) { // 0x01 = JSON datatype
    LOG(ERROR) << "Fail to ADD request";
    return -1;
}
    // request.Set("dummy", "value", 0, 1000, 0); // Will fail, but triggers SASL authentication
    channel.CallMethod(NULL, &cntl, &select_bucket_request, &select_bucket_response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    cntl.Reset();
    if (!select_bucket_request.Get(butil::string_printf("%s", "document-key3"))) {
        LOG(ERROR) << "Fail to GET request";
        return -1;
    }
    channel.CallMethod(NULL, &cntl, &select_bucket_request, &select_bucket_response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    return 0;
}




