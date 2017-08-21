// Baidu RPC - A framework to host and access services throughout Baidu.
// Copyright (c) 2015 Baidu.com, Inc. All Rights Reserved

// Author: The baidu-rpc authors (pbrpc@baidu.com)
// Date: Mon Dec 14 19:12:30 CST 2015

#ifndef BRPC_RPC_DUMP_H
#define BRPC_RPC_DUMP_H

#include <gflags/gflags_declare.h>
#include "base/iobuf.h"                            // IOBuf
#include "base/files/file_path.h"                  // FilePath
#include "bvar/collector.h"
#include "brpc/rpc_dump.pb.h"                 // RpcDumpMeta

namespace base {
class FileEnumerator;
}


namespace brpc {

DECLARE_bool(rpc_dump);

// Randomly take samples of all requests and write into a file in batch in
// a background thread.

// Example:
//   SampledRequest* sample = AskToBeSampled();
//   If (sample) {
//     sample->xxx = yyy;
//     sample->request = ...;
//     sample->Submit();
//   }
//
// In practice, sampled requests are just small fraction of all requests.
// The overhead of sampling should be negligible for overall performance.

struct SampledRequest : public bvar::Collected
                      , public RpcDumpMeta {
    base::IOBuf request;

    // Implement methods of Sampled.
    void dump_and_destroy(size_t round);
    void destroy();
    bvar::CollectorSpeedLimit* speed_limit() {
        extern bvar::CollectorSpeedLimit g_rpc_dump_sl;
        return &g_rpc_dump_sl;
    }
};

// If this function returns non-NULL, the caller must fill the returned
// object and submit it for later dumping by calling SubmitSample(). If
// the caller ignores non-NULL return value, the object is leaked.
inline SampledRequest* AskToBeSampled() {
    extern bvar::CollectorSpeedLimit g_rpc_dump_sl;
    if (!FLAGS_rpc_dump || !bvar::is_collectable(&g_rpc_dump_sl)) {
        return NULL;
    }
    return new (std::nothrow) SampledRequest;
}

// Read samples from dumped files in a directory.
// Example:
//   SampleIterator it("./rpc_dump_echo_server");
//   for (SampleRequest* req = it->Next(); req != NULL; req = it->Next()) {
//     ...
//   }
class SampleIterator {
public:
    explicit SampleIterator(const base::StringPiece& dir);
    ~SampleIterator();

    // Read a sample. Order of samples are not guaranteed to be same with
    // the order that they're stored in dumped files.
    // Returns the sample which should be deleted by caller. NULL means
    // all dumped files are read.
    SampledRequest* Next();

private:
    // Parse on request from the buf. Set `format_error' to true when
    // the buf does not match the format.
    static SampledRequest* Pop(base::IOBuf& buf, bool* format_error);
    
    base::IOPortal _cur_buf;
    int _cur_fd;
    base::FileEnumerator* _enum;
    base::FilePath _dir;
};

} // namespace brpc


#endif  // BRPC_RPC_DUMP_H
