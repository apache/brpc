// Licensed to the Apache Software Foundation (ASF) under one
// or more contributor license agreements.  See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership.  The ASF licenses this file
// to you under the Apache License, Version 2.0 (the
// "License"); you may not use this file except in compliance
// with the License.  You may obtain a copy of the License at
//
//   http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing,
// software distributed under the License is distributed on an
// "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
// KIND, either express or implied.  See the License for the
// specific language governing permissions and limitations
// under the License.


#include <gflags/gflags.h>
#include <fcntl.h>                    // O_CREAT
#include "butil/file_util.h"
#include "butil/raw_pack.h"
#include "butil/unique_ptr.h"
#include "butil/fast_rand.h"
#include "butil/files/file_enumerator.h"
#include "bvar/bvar.h"
#include "brpc/log.h"
#include "brpc/reloadable_flags.h"
#include "brpc/rpc_dump.h"
#include "brpc/protocol.h"

namespace bvar {
std::string read_command_name();
}

namespace brpc {

DECLARE_uint64(max_body_size);

#define DUMPED_FILE_PREFIX "requests"

// Layout of dumped files:
// <rpc_dump_dir>/<DUMPED_FILE_PREFIX>.yyyymmdd_hhmmss_uuuuus
// <rpc_dump_dir>/<DUMPED_FILE_PREFIX>.yyyymmdd_hhmmss_uuuuus
// ...
// <rpc_dump_dir>/<DUMPED_FILE_PREFIX>.yyyymmdd_hhmmss_uuuuus

DEFINE_bool(rpc_dump, false,
            "Dump requests into files so that they can replayed "
            "laterly. Flags prefixed with \"rpc_dump_\" are not effective "
            "until this flag is true");
DEFINE_string(rpc_dump_dir, "./rpc_data/rpc_dump/<app>",
              "The directory of dumped files, will be cleaned "
              "if it exists when this process starts");
DEFINE_int32(rpc_dump_max_files, 32,
             "Max number of dumped files in a directory. "
             "If new file is needed, oldest file is removed.");
DEFINE_int32(rpc_dump_max_requests_in_one_file, 1000,
             "Max number of requests in one dumped file");

BRPC_VALIDATE_GFLAG(rpc_dump, PassValidate);
BRPC_VALIDATE_GFLAG(rpc_dump_max_requests_in_one_file, PositiveInteger);
BRPC_VALIDATE_GFLAG(rpc_dump_max_files, PositiveInteger);

static const size_t UNWRITTEN_BUFSIZE = 1024 * 1024;
static const int64_t FLUSH_TIMEOUT = 2000000L; // 2s

class RpcDumpContext {
public:
    void SaveFlags();

    void SetRound(size_t round);
    
    void Dump(size_t round, SampledRequest*);
    
    static bool Serialize(butil::IOBuf& buf, SampledRequest* sample);
    
    RpcDumpContext()
        : _cur_req_count(0)
        , _cur_fd(-1)
        , _last_round(0)
        , _max_requests_in_one_file(0)
        , _max_files(0)
        , _sched_write_time(butil::gettimeofday_us() + FLUSH_TIMEOUT)
        , _last_file_time(0)
    {
        _command_name = bvar::read_command_name();
        SaveFlags();
        // Clean the directory at fist time.
        butil::DeleteFile(_dir, true); 
    }
    ~RpcDumpContext() {
        if (_cur_fd >= 0) {
            close(_cur_fd);
            _cur_fd = -1;
        }
    }
    
private:
    std::string _command_name;
    int _cur_req_count; // written #req in current file
    int _cur_fd;        // fd of current file
    size_t _last_round;
    // save gflags which could be reloaded at anytime.
    int _max_requests_in_one_file;
    int _max_files;
    int64_t _sched_write_time;     // duetime of last write
    int64_t _last_file_time;  // time for the postfix of last file
    // the queue for remembering oldest file to remove.
    std::deque<std::string> _filenames;
    butil::FilePath _dir;
    // current filename, being here just to reuse memory.
    std::string _cur_filename;
    // buffering output to file so they can be written in batch.
    butil::IOBuf _unwritten_buf;
};

bvar::CollectorSpeedLimit g_rpc_dump_sl = BVAR_COLLECTOR_SPEED_LIMIT_INITIALIZER;
static RpcDumpContext* g_rpc_dump_ctx = NULL;

void SampledRequest::dump_and_destroy(size_t round) {
    static bvar::DisplaySamplingRatio sampling_ratio_var(
        "rpc_dump_sampling_ratio", &g_rpc_dump_sl);
    
    // Safe to modify g_rpc_dump_ctx w/o locking.
    RpcDumpContext* rpc_dump_ctx = g_rpc_dump_ctx;
    if (rpc_dump_ctx == NULL) {
        rpc_dump_ctx = new RpcDumpContext;
        g_rpc_dump_ctx = rpc_dump_ctx;
    }
    rpc_dump_ctx->Dump(round, this);
    destroy();
}

void SampledRequest::destroy() {
    delete this;
}

// Save gflags which could be reloaded at anytime.
void RpcDumpContext::SaveFlags() {
    std::string dir;
    CHECK(GFLAGS_NAMESPACE::GetCommandLineOption("rpc_dump_dir", &dir));
    
    const size_t pos = dir.find("<app>");
    if (pos != std::string::npos) {
        dir.replace(pos, 5/*<app>*/, _command_name);
    }
    _dir = butil::FilePath(dir);

    _max_requests_in_one_file = FLAGS_rpc_dump_max_requests_in_one_file;
    _max_files = FLAGS_rpc_dump_max_files;
}

// Dump a request.
void RpcDumpContext::Dump(size_t round, SampledRequest* sample) {
    if (_last_round != round) {
        _last_round = round;
        SaveFlags();
    }

    if (!Serialize(_unwritten_buf, sample)) {
        return;
    }
    ++_cur_req_count;
    if (_cur_req_count >= _max_requests_in_one_file) {
        // Reach the limit of #request in a file.
        RPC_VLOG << "Write because _cur_req_count=" << _cur_req_count;
    } else if (_unwritten_buf.size() >= UNWRITTEN_BUFSIZE) {
        // Too much unwritten data
        RPC_VLOG << "Write because _unwritten_buf=" << _unwritten_buf.size();
    } else if (butil::gettimeofday_us() >= _sched_write_time) {
        // Not write for a while.
        RPC_VLOG << "Write because timeout";
    } else {
        return;
    }

    // Open file if needed.
    if (_cur_fd < 0) {
        // Make sure the dir exists.
        butil::File::Error error;
        if (!butil::CreateDirectoryAndGetError(_dir, &error)) {
            LOG(ERROR) << "Fail to create directory=`" << _dir.value()
                       << "', " << error;
            return;
        }
        // Remove oldest files.
        while ((int)_filenames.size() >= _max_files && !_filenames.empty()) {
            butil::DeleteFile(butil::FilePath(_filenames.front()), false);
            _filenames.pop_front();
        }
        // Make current time as postfix.
        int64_t cur_file_time = butil::gettimeofday_us();
        // Make postfix monotonic.
        if (cur_file_time <= _last_file_time) {
            cur_file_time = _last_file_time + 1;
        }
        time_t rawtime = cur_file_time / 1000000L;
        struct tm* timeinfo = localtime(&rawtime);
        char ts_buf[64];
        strftime(ts_buf, sizeof(ts_buf), "%Y%m%d_%H%M%S", timeinfo);
        butil::string_printf(&_cur_filename, "%s/" DUMPED_FILE_PREFIX ".%s_%06u",
                            _dir.value().c_str(), ts_buf,
                            (unsigned)(cur_file_time - rawtime * 1000000L));
        _cur_fd = open(_cur_filename.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0666);
        if (_cur_fd < 0) {
            PLOG(ERROR) << "Fail to open " << _cur_filename;
            return;
        }
        _last_file_time = cur_file_time;
        _filenames.push_back(_cur_filename);
    }
    // Write all data in _unwritten_buf. This is different from writing
    // into a socket: local file should always be writable unless error occurs
    bool fail_to_write = false;
    while (!_unwritten_buf.empty()) {
        if (_unwritten_buf.cut_into_file_descriptor(_cur_fd) < 0) {
            if (errno != EINTR && errno != EAGAIN) {
                PLOG(ERROR) << "Fail to write into " << _cur_filename;
                fail_to_write = true;
                break;
            }
        }
    }
    _unwritten_buf.clear();
    _sched_write_time = butil::gettimeofday_us() + FLUSH_TIMEOUT;
    if (fail_to_write || _cur_req_count >= _max_requests_in_one_file) {
        // clean up
        if (_cur_fd >= 0) {
            close(_cur_fd);
            _cur_fd = -1;
        }
        _cur_req_count = 0;
    }
}

bool RpcDumpContext::Serialize(butil::IOBuf& buf, SampledRequest* sample) {
    // Use the header of baidu_std.
    char rpc_header[12];
    butil::IOBuf::Area header_area = buf.reserve(sizeof(rpc_header));
    
    const size_t starting_size = buf.size();
    butil::IOBufAsZeroCopyOutputStream buf_stream(&buf);
    if (!sample->meta.SerializeToZeroCopyStream(&buf_stream)) {
        LOG(ERROR) << "Fail to serialize";
        return false;
    }
    const size_t meta_size = buf.size() - starting_size;
    buf.append(sample->request);

    uint32_t* dummy = (uint32_t*)rpc_header;  // suppress strict-alias warning
    *dummy = *(uint32_t*)"PRPC";
    butil::RawPacker(rpc_header + 4)
        .pack32(meta_size + sample->request.size())
        .pack32(meta_size);
    CHECK_EQ(0, buf.unsafe_assign(header_area, rpc_header));
    return true;
}

SampleIterator::SampleIterator(const butil::StringPiece& dir)
    : _cur_fd(-1)
    , _enum(NULL)
    , _dir(std::string(dir.data(), dir.size())) {
}

SampleIterator::~SampleIterator() {
    if (_cur_fd) {
        ::close(_cur_fd);
        _cur_fd = -1;
    }
    delete _enum;
    _enum = NULL;
}

SampledRequest* SampleIterator::Next() {
    if (!_cur_buf.empty()) {
        bool error = false;
        SampledRequest* r = Pop(_cur_buf, &error);
        if (r) {
            return r;
        }
        if (error) {
            _cur_buf.clear();
            if (_cur_fd >= 0) {
                ::close(_cur_fd);
                _cur_fd = -1;
            }
        }
    }
    while (1) {
        while (_cur_fd >= 0) {
            ssize_t nr = _cur_buf.append_from_file_descriptor(_cur_fd, 524288);
            if (nr < 0) {
                if (errno != EAGAIN && errno != EINTR) {
                    PLOG(ERROR) << "Fail to read fd=" << _cur_fd;
                    break;
                }
            } else if (nr == 0) {  // EOF
                break;
            } else {
                return Next();  // tailr
            }
        }
        _cur_buf.clear();
        if (_cur_fd >= 0) {
            ::close(_cur_fd);
            _cur_fd = -1;
        }
        
        if (_enum == NULL) {
            _enum = new butil::FileEnumerator(
                _dir, false, butil::FileEnumerator::FILES);
        }
        butil::FilePath filename = _enum->Next();
        if (filename.empty()) {
            return NULL;
        }
        _cur_fd = open(filename.value().c_str(), O_RDONLY);
    }
}

SampledRequest* SampleIterator::Pop(butil::IOBuf& buf, bool* format_error) {
    char backing_buf[12];
    const char* p = (const char*)buf.fetch(backing_buf, sizeof(backing_buf));
    if (NULL == p) {  // buf.length() < sizeof(backing_buf)
        return NULL;
    }
    if (*(const uint32_t*)p != *(const uint32_t*)"PRPC") {
        LOG(ERROR) << "Unmatched magic string";
        *format_error = true;
        return NULL;
    }
    uint32_t body_size;
    uint32_t meta_size;
    butil::RawUnpacker(p + 4).unpack32(body_size).unpack32(meta_size);
    if (body_size > FLAGS_max_body_size) {
        LOG(ERROR) << "Too big body=" << body_size;
        *format_error = true;
        return NULL;
    } else if (buf.length() < sizeof(backing_buf) + body_size) {
        return NULL;
    }
    if (meta_size > body_size) {
        LOG(ERROR) << "meta_size=" << meta_size << " is bigger than body_size="
                   << body_size;
        *format_error = true;
        return NULL;
    }
    buf.pop_front(sizeof(backing_buf));
    butil::IOBuf meta_buf;
    buf.cutn(&meta_buf, meta_size);
    std::unique_ptr<SampledRequest> req(new SampledRequest);
    if (!ParsePbFromIOBuf(&req->meta, meta_buf)) {
        LOG(ERROR) << "Fail to parse RpcDumpMeta";
        *format_error = true;
        return NULL;
    }
    buf.cutn(&req->request, body_size - meta_size);
    return req.release();
}

#undef DUMPED_FILE_PREFIX

} // namespace brpc
