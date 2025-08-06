#include <gflags/gflags.h>
#include <bthread/bthread.h>
#include <butil/logging.h>
#include <butil/string_printf.h>
#include <brpc/channel.h>
#include <brpc/couchbase.h>
#include <brpc/policy/couchbase_protocol.h>
#include <brpc/policy/couchbase_authenticator.h>
#include <chrono>
#include <mutex>
#include <iomanip>

// ANSI color codes for console output
#define GREEN "\033[32m"
#define RED "\033[31m"
#define RESET "\033[0m"

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

// Global variables for timing statistics
std::vector<double> thread_response_times;
std::mutex timing_mutex;

// static void* sender(void* arg) {
//     google::protobuf::RpcChannel* channel = 
//         static_cast<google::protobuf::RpcChannel*>(arg);
//     const int base_index = g_sender_count.fetch_add(1, butil::memory_order_relaxed);

//     std::string value;
//     std::string key = "test_brpc_";
//  // Example batch size, can be parameterized

//     brpc::CouchbaseRequest request;
//     for (int i = 0; i < batch_size; ++i) {
//         CHECK(request.Get(butil::string_printf("%s%d", key.c_str(), i)));
//     }
    
//     // Start timing for this thread
//     auto start_time = std::chrono::high_resolution_clock::now();
    
//     while (!brpc::IsAskedToQuit()) {
//         // We will receive response synchronously, safe to put variables
//         // on stack.
//         brpc::CouchbaseResponse response;
//         brpc::Controller cntl;

//         // Because `done'(last parameter) is NULL, this function waits until
//         // the response comes back or error occurs(including timedout).
//         channel->CallMethod(NULL, &cntl, &request, &response, NULL);
//         const int64_t elp = cntl.latency_us();
//         if (!cntl.Failed()) {
//             int i = 0;
//             g_latency_recorder << cntl.latency_us();
//             for (i = 0; i < batch_size; ++i) {
//                 uint32_t flags;
//                 if (!response.PopGet(&value, &flags, NULL)) {
//                     LOG(INFO) << "Fail to GET the key, " << response.LastError();
//                     std::cout<<"thread id "<< bthread_self() << " failed to get key: " << butil::string_printf("%s%d", key.c_str(), i) << std::endl;
//                     break;
//                 }
//                 std::cout<<"thread id "<< bthread_self() <<"Key: " << butil::string_printf("%s%d", key.c_str(), i) << ", Value: " << value << std::endl;
//             }
            
//             // End timing and calculate response time for this thread
//             auto end_time = std::chrono::high_resolution_clock::now();
//             auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
//             double response_time_ms = duration.count() / 1000.0; // Convert to milliseconds
            
//             std::cout << "Thread " << bthread_self() << " GET request took: " << response_time_ms << " ms" << std::endl;
            
//             // Store the response time in thread-safe manner
//             {
//                 std::lock_guard<std::mutex> lock(timing_mutex);
//                 thread_response_times.push_back(response_time_ms);
//             }
            
//             bthread_usleep(5000000); // Sleep for 50ms before exiting
//             return NULL;
            
//         } else {
//             g_error_count << 1; 
            
//             // End timing even for failed requests
//             auto end_time = std::chrono::high_resolution_clock::now();
//             auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);
//             double response_time_ms = duration.count() / 1000.0;
            
//             std::cout << "Thread " << bthread_self() << " GET request failed after: " << response_time_ms << " ms" << std::endl;
            
//             bthread_usleep(5000000);
//             return NULL;
//         }
//     }
//     return NULL;
// }

int main(){
    
    std::vector<std::pair<std::string, long long>> operation_times;
    
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
    std::cout << GREEN << "Channel initialized successfully" << RESET << std::endl;
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
    
    // Start timing for authentication
    auto auth_start_time = std::chrono::high_resolution_clock::now();
    
    channel.CallMethod(NULL, &cntl, &auth_request, &auth_response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    
    // Check authentication response status
    if (!auth_response.LastError().empty()) {
        LOG(ERROR) << "Authentication failed: " << auth_response.LastError();
        return -1;
    }
    
    // End timing for authentication
    auto auth_end_time = std::chrono::high_resolution_clock::now();
    auto auth_duration = std::chrono::duration_cast<std::chrono::microseconds>(auth_end_time - auth_start_time);
    double auth_time_ms = auth_duration.count() / 1000.0;
    
    cntl.Reset();

    std::cout << GREEN << "Authentication successful (took " << auth_time_ms << " ms), proceeding with Couchbase operations..." << RESET << std::endl;

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
    
    // Start timing for bucket selection
    auto bucket_start_time = std::chrono::high_resolution_clock::now();
    
    channel.CallMethod(NULL, &cntl, &select_bucket_request, &select_bucket_response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    
    // Check bucket selection response status
    if (!select_bucket_response.LastError().empty()) {
        LOG(ERROR) << "Bucket selection failed: " << select_bucket_response.LastError();
        return -1;
    }
    
    // End timing for bucket selection
    auto bucket_end_time = std::chrono::high_resolution_clock::now();
    auto bucket_duration = std::chrono::duration_cast<std::chrono::microseconds>(bucket_end_time - bucket_start_time);
    double bucket_time_ms = bucket_duration.count() / 1000.0;
    
    cntl.Reset();

    if (select_bucket_response.LastError().empty()) {
        std::cout << GREEN << "Bucket selected successfully: " << bucket_name << " (took " << bucket_time_ms << " ms)" << RESET << std::endl;
    } else {
        std::cout << RED << "Failed to select bucket: " << select_bucket_response.LastError() << RESET << std::endl;
        return -1;
    }

    //enter batch size 
    // std::cout << "Enter batch size for operations (e.g., 10): ";
    // std::cin >> batch_size;

    // // Add operation
    // brpc::CouchbaseRequest add_request;
    // brpc::CouchbaseResponse add_response;

    // for(int i = 0;i<batch_size;i++) {
    //     if (!add_request.Add(butil::string_printf("test_brpc_%d", i), "{\"hello\":\"world\"}", 0xdeadbeef , FLAGS_exptime, 0)) {
    //         LOG(ERROR) << "Fail to ADD request";
    //         return -1;
    //     }
    // }
    
    // // Start timing for ADD operations
    // auto add_start_time = std::chrono::high_resolution_clock::now();
    
    // channel.CallMethod(NULL, &cntl, &add_request, &add_response, NULL);
    // if (cntl.Failed()) {
    //     LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
    //     return -1;
    // }

    // for (int i = 0; i < batch_size; ++i) {
    //     if (!add_response.PopAdd(NULL)) {
    //         LOG(ERROR) << "Fail to ADD memcache, i=" << i
    //                    << ", " << add_response.LastError();
    //     }
    // }
    
    // // End timing for ADD operations
    // auto add_end_time = std::chrono::high_resolution_clock::now();
    // auto add_duration = std::chrono::duration_cast<std::chrono::microseconds>(add_end_time - add_start_time);
    // double add_time_ms = add_duration.count() / 1000.0;
    
    // std::cout << "Added " << batch_size << " documents (took " << add_time_ms << " ms)" << std::endl;
    
    // cntl.Reset();

    // std::vector<bthread_t> bids;
    // std::vector<pthread_t> pids;
    // if (!FLAGS_use_bthread) {
    //     pids.resize( batch_size);
    //     for (int i = 0; i <  batch_size; ++i) {
    //         if (pthread_create(&pids[i], NULL, sender, &channel) != 0) {
    //             LOG(ERROR) << "Fail to create pthread";
    //             return -1;
    //         }
    //     }
    // } else {
    //     bids.resize( batch_size);
    //     for (int i = 0; i <  batch_size; ++i) {
    //         if (bthread_start_background(
    //                 &bids[i], NULL, sender, &channel) != 0) {
    //             LOG(ERROR) << "Fail to create bthread";
    //             return -1;
    //         }
    //     }
    // }

    // LOG(INFO) << "couchbase_client is going to quit";
    // for (int i = 0; i <  batch_size; ++i) {
    //     if (!FLAGS_use_bthread) {
    //         pthread_join(pids[i], NULL);
    //     } else {
    //         bthread_join(bids[i], NULL);
    //     }
    // }
    
    // // Calculate and print average response time
    // if (!thread_response_times.empty()) {
    //     double total_time = 0.0;
    //     for (double time : thread_response_times) {
    //         total_time += time;
    //     }
    //     double average_response_time = total_time / thread_response_times.size();
        
    //     std::cout << "\n=== Performance Summary ===" << std::endl;
    //     std::cout << "Authentication time: " << auth_time_ms << " ms" << std::endl;
    //     std::cout << "Bucket selection time: " << bucket_time_ms << " ms" << std::endl;
    //     std::cout << "ADD operations time: " << add_time_ms << " ms" << std::endl;
    //     std::cout << "Total threads: " << thread_response_times.size() << std::endl;
    //     std::cout << "Average GET response time: " << average_response_time << " ms" << std::endl;
    //     std::cout << "Total time for all GET requests: " << total_time << " ms" << std::endl;
    //     std::cout << "Total authorization time: " << (auth_time_ms + bucket_time_ms) << " ms" << std::endl;
    //     std::cout << "=========================" << std::endl;
    // } else {
    //     std::cout << "\n=== Performance Summary ===" << std::endl;
    //     std::cout << "Authentication time: " << auth_time_ms << " ms" << std::endl;
    //     std::cout << "Bucket selection time: " << bucket_time_ms << " ms" << std::endl;
    //     std::cout << "ADD operations time: " << add_time_ms << " ms" << std::endl;
    //     std::cout << "Total authorization time: " << (auth_time_ms + bucket_time_ms) << " ms" << std::endl;
    //     std::cout << "No successful GET requests completed." << std::endl;
    //     std::cout << "=========================" << std::endl;
    // }

    // Add operation
    brpc::CouchbaseRequest add_request;
    brpc::CouchbaseResponse add_response;
    if (!add_request.Add(butil::string_printf("user::test_brpc_binprot"), R"({"name": "John Doe", "age": 30, "email": "john@example.com"})", 0xdeadbeef , FLAGS_exptime, 0)) {
        LOG(ERROR) << "Fail to ADD request";
        return -1;
    }
    auto add_start_time = std::chrono::high_resolution_clock::now();
    channel.CallMethod(NULL, &cntl, &add_request, &add_response, NULL);
    auto add_end_time = std::chrono::high_resolution_clock::now();
    auto add_duration = std::chrono::duration_cast<std::chrono::microseconds>(add_end_time - add_start_time);
    operation_times.push_back({"Add user data (first attempt) in microsecond", add_duration.count()});
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    else{
        // Check ADD operation response status
        uint64_t cas_value;
        if (add_response.PopAdd(&cas_value)) {
            std::cout << GREEN << "ADD operation successful, CAS: " << cas_value << RESET << std::endl;
        } else {
            std::cout << RED << add_response.LastError() << RESET << std::endl;
        }
    }

    cntl.Reset();

    if (!add_request.Add(butil::string_printf("user::test_brpc_binprot"), R"({"name": "John Doe", "age": 30, "email": "john@example.com"})", 0xdeadbeef , FLAGS_exptime, 0)) {
        LOG(ERROR) << "Fail to ADD request";
        return -1;
    }
    add_start_time = std::chrono::high_resolution_clock::now();
    channel.CallMethod(NULL, &cntl, &add_request, &add_response, NULL);
    add_end_time = std::chrono::high_resolution_clock::now();
    add_duration = std::chrono::duration_cast<std::chrono::microseconds>(add_end_time - add_start_time);
    operation_times.push_back({"Add user data (Second attempt - expected failure) in microsecond ", add_duration.count()});

    // Check second ADD operation response status (should fail with key exists)
    if (cntl.Failed()) {
        LOG(ERROR) << "RPC call failed: " << cntl.ErrorText();
    } else {
        uint64_t cas_value;
        if (add_response.PopAdd(&cas_value)) {
            std::cout << GREEN << "Second ADD operation unexpectedly successful, CAS: " << cas_value << RESET << std::endl;
        } else {
            std::cout << RED << "Second ADD operation failed as expected: " << add_response.LastError() << RESET << std::endl;
        }
    }

    cntl.Reset();
    // Get operation
    brpc::CouchbaseRequest get_request;
    brpc::CouchbaseResponse get_response;
    if (!get_request.Get(butil::string_printf("%s", "user::test_brpc_binprot"))) {
        LOG(ERROR) << "Fail to GET request";
        return -1;
    }
    add_start_time = std::chrono::high_resolution_clock::now();
    channel.CallMethod(NULL, &cntl, &get_request, &get_response, NULL);
    add_end_time = std::chrono::high_resolution_clock::now();
    add_duration = std::chrono::duration_cast<std::chrono::microseconds>(add_end_time - add_start_time);
    operation_times.push_back({"Get user data in microsecond ", add_duration.count()});
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    
    // Check GET operation response status
    std::string value;
    uint32_t flags = 0;
    uint64_t cas = 0;
    if (get_response.PopGet(&value, &flags, &cas)) {
        std::cout << GREEN << "GET operation successful" << RESET << std::endl;
        std::cout << "GET value: " << value << std::endl;
        std::cout << "Flags: " << flags << std::endl;
        std::cout << "CAS: " << cas << std::endl;
    } else {
        std::cout << RED << "GET operation failed: " << get_response.LastError() << RESET << std::endl;
        std::cout << "Raw response (hex): ";
        for (char c : get_response.raw_buffer().to_string()) {
            printf("%02x ", static_cast<unsigned char>(c));
        }
        std::cout << std::endl;
    }
    cntl.Reset();
    
    // Create a new request for binprot item1
    brpc::CouchbaseRequest add_request1;
    brpc::CouchbaseResponse add_response1;
    if (!add_request1.Add(butil::string_printf("binprot_item1"), R"({"name": "John Doe", "age": 30, "email": "john@example.com"})", 0xdeadbeef , FLAGS_exptime, 0)) {
        LOG(ERROR) << "Fail to ADD request";
        return -1;
    }
    std::cout << "Sending ADD request for binprot_item1, pipelined count: " << add_request1.pipelined_count() << std::endl;
    add_start_time = std::chrono::high_resolution_clock::now();
    channel.CallMethod(NULL, &cntl, &add_request1, &add_response1, NULL);
     add_end_time = std::chrono::high_resolution_clock::now();
     add_duration = std::chrono::duration_cast<std::chrono::microseconds>(add_end_time - add_start_time);
    operation_times.push_back({"Add binprot item1 in microsecond", add_duration.count()});
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    
    // Check ADD binprot item1 response status
    uint64_t cas_value;
    if (add_response1.PopAdd(&cas_value)) {
        std::cout << GREEN << "ADD binprot item1 successful, CAS: " << cas_value << RESET << std::endl;
    } else {
        std::cout << RED << "ADD binprot item1 failed: " << add_response1.LastError() << RESET << std::endl;
    }
    cntl.Reset();
    
    // Create a new request for binprot item2
    brpc::CouchbaseRequest add_request2;
    brpc::CouchbaseResponse add_response2;
    if (!add_request2.Add(butil::string_printf("binprot_item2"), R"({"name": "John Doe", "age": 30, "email": "john@example.com"})", 0xdeadbeef , FLAGS_exptime, 0)) {
        LOG(ERROR) << "Fail to ADD request";
        return -1;
    }
    std::cout << "Sending ADD request for binprot_item2, pipelined count: " << add_request2.pipelined_count() << std::endl;
     add_start_time = std::chrono::high_resolution_clock::now();
    channel.CallMethod(NULL, &cntl, &add_request2, &add_response2, NULL);
     add_end_time = std::chrono::high_resolution_clock::now();
     add_duration = std::chrono::duration_cast<std::chrono::microseconds>(add_end_time - add_start_time);
    operation_times.push_back({"Add binprot item2 in microsecond", add_duration.count()});
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    
    // Check ADD binprot item2 response status
    if (add_response2.PopAdd(&cas_value)) {
        std::cout << GREEN << "ADD binprot item2 successful, CAS: " << cas_value << RESET << std::endl;
    } else {
        std::cout << RED << "ADD binprot item2 failed: " << add_response2.LastError() << RESET << std::endl;
    }
    cntl.Reset();
    
    // Create a new request for binprot item3
    brpc::CouchbaseRequest add_request3;
    brpc::CouchbaseResponse add_response3;
    if (!add_request3.Add(butil::string_printf("binprot_item3"), R"({"name": "John Doe", "age": 30, "email": "john@example.com"})", 0xdeadbeef , FLAGS_exptime, 0)) {
        LOG(ERROR) << "Fail to ADD request";
        return -1;
    }
    std::cout << "Sending ADD request for binprot_item3, pipelined count: " << add_request3.pipelined_count() << std::endl;
     add_start_time = std::chrono::high_resolution_clock::now();
    channel.CallMethod(NULL, &cntl, &add_request3, &add_response3, NULL);
     add_end_time = std::chrono::high_resolution_clock::now();
     add_duration = std::chrono::duration_cast<std::chrono::microseconds>(add_end_time - add_start_time);
    operation_times.push_back({"Add binprot item3 in microsecond", add_duration.count()});
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    
    // Check ADD binprot item3 response status
    if (add_response3.PopAdd(&cas_value)) {
        std::cout << GREEN << "ADD binprot item3 successful, CAS: " << cas_value << RESET << std::endl;
    } else {
        std::cout << RED << "ADD binprot item3 failed: " << add_response3.LastError() << RESET << std::endl;
    }
    cntl.Reset();

    //pipeline ADD operation
    brpc::CouchbaseRequest pipeline_request;
    brpc::CouchbaseResponse pipeline_response;
    for (int i = 0; i < 10; ++i) {
        if (!pipeline_request.Add(butil::string_printf("pipeline_item_%d", i), R"({"name": "Pipeline User", "age": 25, "email": "pipeline@example.com"})", 0xdeadbeef , FLAGS_exptime, 0)) {
            LOG(ERROR) << "Fail to ADD request";
            return -1;
        }
    }
    std::cout << "Sending pipeline ADD request, pipelined count: " << pipeline_request.pipelined_count() << std::endl;
    channel.CallMethod(NULL, &cntl, &pipeline_request, &pipeline_response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    
    // Check each pipelined operation response status
    std::cout << "Processing pipeline responses..." << std::endl;
    int successful_operations = 0;
    int failed_operations = 0;
    
    for (int i = 0; i < 10; ++i) {
        uint64_t cas_value;
        if (pipeline_response.PopAdd(&cas_value)) {
            std::cout << GREEN << "Pipeline ADD operation " << i << " successful, CAS: " << cas_value << RESET << std::endl;
            successful_operations++;
        } else {
            std::cout << RED << "Pipeline ADD operation " << i << " failed: " << pipeline_response.LastError() << RESET << std::endl;
            failed_operations++;
        }
    }
    
    std::cout << "Pipeline summary: " << successful_operations << " successful, " 
              << failed_operations << " failed operations" << std::endl;

    cntl.Reset();

    // Example of mixed pipeline operations (GET operations on existing and non-existing keys)
    std::cout << "\n=== Testing Mixed Pipeline Operations ===" << std::endl;
    brpc::CouchbaseRequest mixed_pipeline_request;
    brpc::CouchbaseResponse mixed_pipeline_response;
    
    // Add some GET operations to the pipeline - some will succeed, some will fail
    std::vector<std::string> keys_to_get = {
        "user::test_brpc_binprot",  // Should exist
        "binprot_item1",            // Should exist  
        "nonexistent_key1",         // Should fail
        "binprot_item2",            // Should exist
        "nonexistent_key2",         // Should fail
        "binprot_item3"             // Should exist
    };
    
    for (const auto& key : keys_to_get) {
        if (!mixed_pipeline_request.Get(key)) {
            LOG(ERROR) << "Fail to add GET request for key: " << key;
            return -1;
        }
    }
    
    std::cout << "Sending mixed pipeline GET request, pipelined count: " << mixed_pipeline_request.pipelined_count() << std::endl;
    channel.CallMethod(NULL, &cntl, &mixed_pipeline_request, &mixed_pipeline_response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    
    // Process each GET response
    int successful_gets = 0;
    int failed_gets = 0;
    
    for (size_t i = 0; i < keys_to_get.size(); ++i) {
        std::string value;
        uint32_t flags;
        uint64_t cas;
        
        if (mixed_pipeline_response.PopGet(&value, &flags, &cas)) {
            std::cout << GREEN << "GET '" << keys_to_get[i] << "' successful - Value length: " << value.length() 
                      << ", Flags: " << flags << ", CAS: " << cas << RESET << std::endl;
            successful_gets++;
        } else {
            std::cout << RED << "GET '" << keys_to_get[i] << "' failed: " << mixed_pipeline_response.LastError() << RESET << std::endl;
            failed_gets++;
        }
    }
    
    std::cout << "Mixed pipeline summary: " << successful_gets << " successful, " 
              << failed_gets << " failed GET operations" << std::endl;


    cntl.Reset();

    //perform an upsert on the existing key
    brpc::CouchbaseRequest upsert_request;
    brpc::CouchbaseResponse upsert_response;
    if (!upsert_request.Upsert(butil::string_printf("user::test_brpc_binprot"), R"({"name": "Upserted Jane Doe", "age": 28, "email": "upserted.doe@example.com"})", 0, FLAGS_exptime, 0)) {
        LOG(ERROR) << "Fail to add UPSERT request for key: user::test_brpc_binprot";
        return -1;
    }

    channel.CallMethod(NULL, &cntl, &upsert_request, &upsert_response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    // Check UPSERT operation response status
    if (upsert_response.PopUpsert(&cas_value)) {
        std::cout << GREEN << "UPSERT operation successful when the document exists in the server, CAS: " << cas_value << RESET << std::endl;
    } else {
        std::cout << RED << "UPSERT operation failed, when the document does exists in the server: " << upsert_response.LastError() << RESET << std::endl;
    }

    cntl.Reset();
    //do the upsert operation
    brpc::CouchbaseRequest upsert_request_new_doc;
    brpc::CouchbaseResponse upsert_response_new_doc;
    if (!upsert_request_new_doc.Upsert(butil::string_printf("user::test_brpc_new_upsert"), R"({"name": "Jane Doe", "age": 28, "email": "jane.doe@example.com"})", 0, FLAGS_exptime, 0)) {
        LOG(ERROR) << "Fail to add UPSERT request for key: user::test_brpc_new_upsert";
        return -1;
    }

    channel.CallMethod(NULL, &cntl, &upsert_request_new_doc, &upsert_response_new_doc, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    // Check UPSERT operation response status
    if (upsert_response_new_doc.PopUpsert(&cas_value)) {
        std::cout << GREEN << "UPSERT operation successful when the document doesn't exists in the server, CAS: " << cas_value << RESET << std::endl;
    } else {
        std::cout << RED << "UPSERT operation failed when document does not exists in the server: " << upsert_response_new_doc.LastError() << RESET << std::endl;
    }
    
    cntl.Reset();

    //check the upserted data
    brpc::CouchbaseRequest check_upsert_request;
    brpc::CouchbaseResponse check_upsert_response;
    if (!check_upsert_request.Get(butil::string_printf("user::test_brpc_new_upsert"))) {
        LOG(ERROR) << "Fail to GET request for key: user::test_brpc_new_upsert";
        return -1;
    }

    std::cout << "Sending GET request for user::test_brpc_new_upsert, pipelined count: " << check_upsert_request.pipelined_count() << std::endl;
    channel.CallMethod(NULL, &cntl, &check_upsert_request, &check_upsert_response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    
    // Check GET operation response status
    if (check_upsert_response.PopGet(&value, &flags, &cas)) {
        std::cout << GREEN << "GET after UPSERT operation successful - Value: " << value 
                  << ", Flags: " << flags << ", CAS: " << cas << RESET << std::endl;
    } else {
        std::cout << RED << "GET after UPSERT operation failed: " << check_upsert_response.LastError() << RESET << std::endl;
    }
    
    cntl.Reset();

    //delete the Non-existent Key
    brpc::CouchbaseRequest delete_request;
    brpc::CouchbaseResponse delete_response;
    if (!delete_request.Delete(butil::string_printf("Nonexistent_key"))) {
        LOG(ERROR) << "Fail to DELETE request for key: Nonexistent_key";
        return -1;
    }

    std::cout << "Sending DELETE request for Nonexistent_key, pipelined count: " << delete_request.pipelined_count() << std::endl;
    channel.CallMethod(NULL, &cntl, &delete_request, &delete_response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    
    // Check DELETE operation response status
    if (delete_response.PopDelete()) {
        std::cout << GREEN << "DELETE operation successful" << RESET << std::endl;
    } else {
        std::cout << RED << "DELETE operation failed: as expected" << delete_response.LastError() << RESET << std::endl;
    }
    
    cntl.Reset();

    //delete the existing key
    brpc::CouchbaseRequest delete_existing_request;
    brpc::CouchbaseResponse delete_existing_response;
    if (!delete_existing_request.Delete(butil::string_printf("user::test_brpc_binprot"))) {
        LOG(ERROR) << "Fail to DELETE request for key: user::test_brpc_binprot";
        return -1;
    }

    std::cout << "Sending DELETE request for user::test_brpc_binprot, pipelined count: " << delete_existing_request.pipelined_count() << std::endl;
    channel.CallMethod(NULL, &cntl, &delete_existing_request, &delete_existing_response, NULL);
    if (cntl.Failed()) {
        LOG(ERROR) << "Fail to access Couchbase, " << cntl.ErrorText();
        return -1;
    }
    
    // Check DELETE operation response status
    if (delete_existing_response.PopDelete()) {
        std::cout << GREEN << "DELETE operation successful" << RESET << std::endl;
    } else {
        std::cout << RED << "DELETE operation failed: " << delete_existing_response.LastError() << RESET << std::endl;
    }
    
    cntl.Reset();

    // Print operation times
    long long total_time = 0;
    for (const auto& op : operation_times) {
        std::cout << std::left << std::setw(40) << op.first << ": ";
        if (op.second >= 1000) {
            std::cout << std::right << std::setw(8) << (op.second / 1000.0) << " ms" << std::endl;
        } else {
            std::cout << std::right << std::setw(8) << op.second << " μs" << std::endl;
        }
        total_time += op.second;
    }
    return 0;
}




