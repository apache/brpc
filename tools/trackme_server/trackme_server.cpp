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


// A server to receive TrackMeRequest and send back TrackMeResponse.

#include <gflags/gflags.h>
#include <memory>
#include <butil/logging.h>
#include <brpc/server.h>
#include <butil/files/file_watcher.h>
#include <butil/files/scoped_file.h>
#include <brpc/trackme.pb.h>

DEFINE_string(bug_file, "./bugs", "A file containing revision and information of bugs");
DEFINE_int32(port, 8877, "TCP Port of this server");
DEFINE_int32(reporting_interval, 300, "Reporting interval of clients");

struct RevisionInfo {
    int64_t min_rev;
    int64_t max_rev;
    brpc::TrackMeSeverity severity;
    std::string error_text;
};

// Load bugs from a file periodically.
class BugsLoader {
public:
    BugsLoader();
    bool start(const std::string& bugs_file);
    void stop();
    bool find(int64_t revision, brpc::TrackMeResponse* response);
private:
    void load_bugs();
    void run();
    static void* run_this(void* arg);
    typedef std::vector<RevisionInfo> BugList;
    std::string _bugs_file;
    bool _started;
    bool _stop;
    pthread_t _tid;
    std::shared_ptr<BugList> _bug_list;
};

class TrackMeServiceImpl : public brpc::TrackMeService {
public:
    explicit TrackMeServiceImpl(BugsLoader* bugs) : _bugs(bugs) {
    }
    ~TrackMeServiceImpl() {}
    void TrackMe(google::protobuf::RpcController* cntl_base,
                 const brpc::TrackMeRequest* request,
                 brpc::TrackMeResponse* response,
                 google::protobuf::Closure* done) {
        brpc::ClosureGuard done_guard(done);
        brpc::Controller* cntl = (brpc::Controller*)cntl_base;
        // Set to OK by default.
        response->set_severity(brpc::TrackMeOK);
        // Check if the version is affected by bugs if client set it.
        if (request->has_rpc_version()) {
            _bugs->find(request->rpc_version(), response);
        } 
        response->set_new_interval(FLAGS_reporting_interval);
        butil::EndPoint server_addr;
        CHECK_EQ(0, butil::str2endpoint(request->server_addr().c_str(), &server_addr));
        // NOTE(gejun): The ip reported is inaccessible in many cases, use 
        // remote_side instead right now.
        server_addr.ip = cntl->remote_side().ip;
        LOG(INFO) << "Pinged by " << server_addr << " (r"
                  << request->rpc_version() << ")";
    }

private:
    BugsLoader* _bugs;
};

int main(int argc, char* argv[]) {
    GFLAGS_NS::ParseCommandLineFlags(&argc, &argv, true);
    
    brpc::Server server;
    server.set_version("trackme_server");
    BugsLoader bugs;
    if (!bugs.start(FLAGS_bug_file)) {
        LOG(ERROR) << "Fail to start BugsLoader";
        return -1;
    }
    TrackMeServiceImpl echo_service_impl(&bugs);
    if (server.AddService(&echo_service_impl, 
                          brpc::SERVER_DOESNT_OWN_SERVICE) != 0) {
        LOG(ERROR) << "Fail to add service";
        return -1;
    }
    brpc::ServerOptions options;
    // I've noticed that many connections do not report. Don't know the
    // root cause yet. Set the idle_time to keep connections clean.
    options.idle_timeout_sec = FLAGS_reporting_interval * 2;
    if (server.Start(FLAGS_port, &options) != 0) {
        LOG(ERROR) << "Fail to start TrackMeServer";
        return -1;
    }
    server.RunUntilAskedToQuit();
    bugs.stop();
    return 0;
}

BugsLoader::BugsLoader()
    : _started(false)
    , _stop(false)
    , _tid(0)
{}

bool BugsLoader::start(const std::string& bugs_file) {
    _bugs_file = bugs_file;
    if (pthread_create(&_tid, NULL, run_this, this) != 0) {
        LOG(ERROR) << "Fail to create loading thread";
        return false;
    }
    _started = true;
    return true;
}

void BugsLoader::stop() {
    if (!_started) {
        return;
    }
    _stop = true;
    pthread_join(_tid, NULL);
}

void* BugsLoader::run_this(void* arg) {
    ((BugsLoader*)arg)->run();
    return NULL;
}

void BugsLoader::run() {
    // Check status of _bugs_files periodically.
    butil::FileWatcher fw;
    if (fw.init(_bugs_file.c_str()) < 0) {
        LOG(ERROR) << "Fail to init FileWatcher on `" << _bugs_file << "'";
        return;
    }
    while (!_stop) {
        load_bugs();
        while (!_stop) {
            butil::FileWatcher::Change change = fw.check_and_consume();
            if (change > 0) {
                break;
            }
            if (change < 0) {
                LOG(ERROR) << "`" << _bugs_file << "' was deleted";
            }
            sleep(1);
        }
    }

}

void BugsLoader::load_bugs() {
    butil::ScopedFILE fp(fopen(_bugs_file.c_str(), "r"));
    if (!fp) {
        PLOG(WARNING) << "Fail to open `" << _bugs_file << '\'';
        return;
    }

    char* line = NULL;
    size_t line_len = 0;
    ssize_t nr = 0;
    int nline = 0;
    std::unique_ptr<BugList> m(new BugList);
    while ((nr = getline(&line, &line_len, fp.get())) != -1) {
        ++nline;
        if (line[nr - 1] == '\n') { // remove ending newline
            --nr;
        }
        // line format: 
        //   min_rev <sp> max_rev <sp> severity <sp> description
        butil::StringMultiSplitter sp(line, line + nr, " \t");
        if (!sp) {
            continue;
        }
        long long min_rev;
        if (sp.to_longlong(&min_rev)) {
            LOG(WARNING) << "[line" << nline << "] Fail to parse column1 as min_rev";
            continue;
        }
        ++sp;
        long long max_rev;
        if (!sp || sp.to_longlong(&max_rev)) {
            LOG(WARNING) << "[line" << nline << "] Fail to parse column2 as max_rev";
            continue;
        }
        if (max_rev < min_rev) {
            LOG(WARNING) << "[line" << nline << "] max_rev=" << max_rev
                         << " is less than min_rev=" << min_rev;
            continue;
        }
        ++sp;
        if (!sp) {
            LOG(WARNING) << "[line" << nline << "] Fail to parse column3 as severity";
            continue;
        }
        brpc::TrackMeSeverity severity = brpc::TrackMeOK;
        butil::StringPiece severity_str(sp.field(), sp.length());
        if (severity_str == "f" || severity_str == "F") {
            severity = brpc::TrackMeFatal;
        } else if (severity_str == "w" || severity_str == "W") {\
            severity = brpc::TrackMeWarning;
        } else {
            LOG(WARNING) << "[line" << nline << "] Invalid severity=" << severity_str;
            continue;
        }
        ++sp;
        if (!sp) {
            LOG(WARNING) << "[line" << nline << "] Fail to parse column4 as string";
            continue;
        }
        // Treat everything until end of the line as description. So don't add 
        // comments starting with # or //, they are not recognized.
        butil::StringPiece description(sp.field(), line + nr - sp.field());
        RevisionInfo info;
        info.min_rev = min_rev;
        info.max_rev = max_rev;
        info.severity = severity;
        info.error_text.assign(description.data(), description.size());
        m->push_back(info);
    }
    LOG(INFO) << "Loaded " << m->size() << " bugs";
    free(line);
    // Just reseting the shared_ptr. Previous BugList will be destroyed when
    // no threads reference it.
    _bug_list.reset(m.release());
}

bool BugsLoader::find(int64_t revision, brpc::TrackMeResponse* response) {
    // Add reference to make sure the bug list is not deleted.
    std::shared_ptr<BugList> local_list = _bug_list;
    if (local_list.get() == NULL) {
        return false;
    }
    // Reading the list in this function is always safe because a BugList
    // is never changed after creation.
    bool found = false;
    for (size_t i = 0; i < local_list->size(); ++i) {
        const RevisionInfo & info = (*local_list)[i];
        if (info.min_rev <= revision && revision <= info.max_rev) {
            found = true;
            if (info.severity > response->severity()) {
                response->set_severity(info.severity);
            }
            if (info.severity != brpc::TrackMeOK) {
                std::string* error = response->mutable_error_text();
                char prefix[64];
                if (info.min_rev != info.max_rev) {
                    snprintf(prefix, sizeof(prefix), "[r%lld-r%lld] ",
                             (long long)info.min_rev, (long long)info.max_rev);
                } else {
                    snprintf(prefix, sizeof(prefix), "[r%lld] ",
                             (long long)info.min_rev);
                }
                error->append(prefix);
                error->append(info.error_text);
                error->append("; ");
            }
        }
    }
    return found;
}
