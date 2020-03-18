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


#include "bthread/bthread.h"                  // bthread_id_xx
#include "bthread/unstable.h"                 // bthread_timer_add
#include "butil/atomicops.h"
#include "butil/time.h"
#include "butil/macros.h"
#include "brpc/details/controller_private_accessor.h"
#include "brpc/parallel_channel.h"


namespace brpc {

ParallelChannelOptions::ParallelChannelOptions()
    : timeout_ms(500)
    , fail_limit(-1) {
}

DECLARE_bool(usercode_in_pthread);

// Not see difference when memory is cached.
#ifdef BRPC_CACHE_PCHAN_MEM
struct Memory {
    int size;
    void* ptr;
};
static __thread Memory tls_cached_pchan_mem = { 0, NULL };
#endif

class ParallelChannelDone : public google::protobuf::Closure {
private:
    ParallelChannelDone(int fail_limit, int ndone, int nchan, int memsize,
                        Controller* cntl, google::protobuf::Closure* user_done)
        : _fail_limit(fail_limit)
        , _ndone(ndone)
        , _nchan(nchan)
        , _memsize(memsize)
        , _current_fail(0)
        , _current_done(0)
        , _cntl(cntl)
        , _user_done(user_done)
        , _callmethod_bthread(INVALID_BTHREAD)
        , _callmethod_pthread(0) {
    }

    ~ParallelChannelDone() { }

public:
    class SubDone : public google::protobuf::Closure {
    public:
        SubDone() : shared_data(NULL) {
        }

        ~SubDone() {
            // Can't delete request/response in ~SubCall because the
            // object is copyable.
            if (ap.flags & DELETE_REQUEST) {
                delete ap.request;
            }
            if (ap.flags & DELETE_RESPONSE) {
                delete ap.response;
            }
        }
 
        void Run() {
            shared_data->OnSubDoneRun(this);
        }

        ParallelChannelDone* shared_data;
        butil::intrusive_ptr<ResponseMerger> merger;
        SubCall ap;
        Controller cntl;
    };
    
    static ParallelChannelDone* Create(
        int fail_limit, int ndone, const SubCall* aps, int nchan,
        Controller* cntl, google::protobuf::Closure* user_done) {
        // We need to create the object in this way because _sub_done is
        // dynamically allocated.
        // The memory layout:
        //   ParallelChannelDone
        //   SubDone1       `
        //   SubDone2       - ndone
        //   ...            /
        //   SubDoneIndex1  `
        //   SubDoneIndex2  - nchan, existing when nchan != ndone
        //   ...            /
        size_t req_size = offsetof(ParallelChannelDone, _sub_done) +
            sizeof(SubDone) * ndone;
        if (ndone != nchan) {
            req_size += sizeof(int) * nchan;
        }
        void* mem = NULL;
        int memsize = 0;
#ifdef BRPC_CACHE_PCHAN_MEM
        Memory pchan_mem = tls_cached_pchan_mem;
        if (pchan_mem.size >= req_size) {  // use tls if it's big enough
            mem = pchan_mem.ptr;
            memsize = pchan_mem.size;
            pchan_mem.size = 0;
            pchan_mem.ptr = NULL;
            tls_cached_pchan_mem = pchan_mem;
        } else {
            mem = malloc(req_size);
            memsize = req_size;
            if (BAIDU_UNLIKELY(NULL == mem)) {
                return NULL;
            }
        }
#else
        mem = malloc(req_size);
        memsize = req_size;
        if (BAIDU_UNLIKELY(NULL == mem)) {
            return NULL;
        }
#endif
        ParallelChannelDone* d = new (mem) ParallelChannelDone(
            fail_limit, ndone, nchan, memsize, cntl, user_done);

        // Apply client settings of _cntl to controllers of sub calls, except
        // timeout. If we let sub channel do their timeout separately, when
        // timeout happens, we get ETOOMANYFAILS rather than ERPCTIMEDOUT.
        Controller::ClientSettings settings;
        cntl->SaveClientSettings(&settings);
        settings.timeout_ms = -1;
        for (int i = 0; i < ndone; ++i) {
            new (d->sub_done(i)) SubDone;
            d->sub_done(i)->cntl.ApplyClientSettings(settings);
            d->sub_done(i)->cntl.allow_done_to_run_in_place();
        }
        // Setup the map for finding sub_done of i-th sub_channel
        if (ndone != nchan) {
            int done_index = 0;
            for (int i = 0; i < nchan; ++i) {
                if (aps[i].is_skip()) {
                    d->sub_done_map(i) = -1;
                } else {
                    d->sub_done_map(i) = done_index++;
                }
            }
            CHECK_EQ(ndone, done_index);
        }
        return d;
    }

    static void Destroy(ParallelChannelDone* d) {
        if (d != NULL) {
            for (int i = 0; i < d->_ndone; ++i) {
                d->sub_done(i)->~SubDone();
            }
#ifdef BRPC_CACHE_PCHAN_MEM
            Memory pchan_mem = tls_cached_pchan_mem;
            if (pchan_mem.size != 0) {
                // free the memory if tls already has sth.
                d->~ParallelChannelDone();
                free(d);
            } else {
                pchan_mem.size = d->_memsize;
                pchan_mem.ptr = d;
                d->~ParallelChannelDone();
                tls_cached_pchan_mem = pchan_mem;
            }
#else
            d->~ParallelChannelDone();
            free(d);
#endif
        }
    }

    void Run() {
        const int ec = _cntl->ErrorCode();
        if (ec == EPCHANFINISH) {
            // all sub calls finished. Clear the error and we'll set
            // successfulness of _cntl in OnSubDoneRun().
            _cntl->_error_code = 0;
            _cntl->_error_text.clear();
        } else {
            CHECK(ECANCELED == ec || ERPCTIMEDOUT == ec) << "ec=" << ec;
        }
        OnSubDoneRun(NULL);
    }

    static void* RunOnComplete(void* arg) {
        static_cast<ParallelChannelDone*>(arg)->OnComplete();
        return NULL;
    }

    // For otherwhere to know if they're in the same thread.
    void SaveThreadInfoOfCallsite() {
        _callmethod_bthread = bthread_self();
        if (_callmethod_bthread == INVALID_BTHREAD) {
            _callmethod_pthread = pthread_self();
        }
    }

    bool IsSameThreadAsCallMethod() const {
        if (_callmethod_bthread != INVALID_BTHREAD) {
            return bthread_self() == _callmethod_bthread;
        }
        return pthread_self() == _callmethod_pthread;
    }
    
    void OnSubDoneRun(SubDone* fin) {
        if (fin != NULL) {
            // [ called from SubDone::Run() ]

            // Count failed sub calls, if fail_limit is reached, cancel others.
            if (fin->cntl.FailedInline() &&
                _current_fail.fetch_add(1, butil::memory_order_relaxed) + 1
                == _fail_limit) {
                for (int i = 0; i < _ndone; ++i) {
                    SubDone* sd = sub_done(i);
                    if (fin != sd) {
                        bthread_id_error(sd->cntl.call_id(), ECANCELED);
                    }
                }
            }
            // NOTE: Don't access any member after the fetch_add because
            // another thread may already go down and Destroy()-ed this object.
            const uint32_t saved_ndone = _ndone;
            const CallId saved_cid = _cntl->_correlation_id;
            // Add 1 to finished sub calls.
            // The release fence is matched with acquire fence below to
            // guarantee visibilities of all other variables.
            const uint32_t val =
                _current_done.fetch_add(1, butil::memory_order_release);
            // Lower 31 bits are number of finished sub calls. If caller is not
            // the last call that finishes, return.
            if ((val & 0x7fffffff) + 1 != saved_ndone) {
                return;
            }
            // If _cntl->call_id() is still there, stop it by sending a special
            // error(which will be cleared) and return.
            if (!(val & 0x80000000)) {
                bthread_id_error(saved_cid, EPCHANFINISH);
                return;
            }
        } else {
            // [ Called from this->Run() ]
            
            // We may cancel sub calls even if all sub calls finish because
            // of reading the value relaxly (and CPU cache is not sync yet).
            // It's OK and we have to, because sub_done can't be accessed
            // after fetch_or.
            uint32_t val = _current_done.load(butil::memory_order_relaxed);
            // Lower 31 bits are number of finished sub calls. Cancel sub calls
            // if not all of them finish.
            if ((val & 0x7fffffff) != (uint32_t)_ndone) {
                for (int i = 0; i < _ndone; ++i) {
                    bthread_id_error(sub_done(i)->cntl.call_id(), ECANCELED);
                }
            }
            // NOTE: Don't access any member after the fetch_or because
            // another thread may already go down and Destroy()-ed this object.
            const int saved_ndone = _ndone;
            // Modify MSB to mark that this->Run() run.
            // The release fence is matched with acquire fence below to
            // guarantee visibilities of all other variables.
            val = _current_done.fetch_or(0x80000000, butil::memory_order_release);
            // If not all sub calls finish, return.
            if ((val & 0x7fffffff) != (uint32_t)saved_ndone) {
                return;
            }
        }
        butil::atomic_thread_fence(butil::memory_order_acquire);

        if (fin != NULL &&
            !_cntl->is_done_allowed_to_run_in_place() &&
            IsSameThreadAsCallMethod()) {
            // A sub channel's CallMethod calls a subdone directly, create a
            // thread to run OnComplete.
            bthread_t bh;
            bthread_attr_t attr = (FLAGS_usercode_in_pthread ?
                                   BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL);
            if (bthread_start_background(&bh, &attr, RunOnComplete, this) != 0) {
                LOG(FATAL) << "Fail to start bthread";
                OnComplete();
            }
        } else {
            OnComplete();
        }
    }

    void OnComplete() {
        // [ Rendezvous point ]
        // One and only one thread arrives here.
        // all call_id of sub calls are destroyed and call_id of _cntl is
        // still locked (because FLAGS_DESTROY_CID_IN_DONE is true);

        // Merge responses of successful calls if fail_limit is not reached.
        // nfailed may be increased during the merging.
        // NOTE: Don't forget to set "nfailed = _ndone" when the _cntl is set
        // to be failed since the RPC is still considered to be successful if
        // nfailed is less than fail_limit
        int nfailed = _current_fail.load(butil::memory_order_relaxed);
        if (nfailed < _fail_limit) {
            for (int i = 0; i < _ndone; ++i) {
                SubDone* sd = sub_done(i);
                google::protobuf::Message* sub_res = sd->cntl._response;
                if (!sd->cntl.FailedInline()) {  // successful calls only.
                    if (sd->merger == NULL) {
                        try {
                            _cntl->_response->MergeFrom(*sub_res);
                        } catch (const std::exception& e) {
                            nfailed = _ndone;
                            _cntl->SetFailed(ERESPONSE, "%s", e.what());
                            break;
                        }
                    } else {
                        ResponseMerger::Result res =
                            sd->merger->Merge(_cntl->_response, sub_res);
                        switch (res) {
                        case ResponseMerger::MERGED:
                            break;
                        case ResponseMerger::FAIL:
                            ++nfailed;
                            break;
                        case ResponseMerger::FAIL_ALL:
                            nfailed = _ndone;
                            _cntl->SetFailed(
                                ERESPONSE,
                                "Fail to merge response of channel[%d]", i);
                            break;
                        }
                    }
                }
            }
        }

        // Note: 1 <= _fail_limit <= _ndone.
        if (nfailed >= _fail_limit) {
            // If controller was already failed, don't change it.
            if (!_cntl->FailedInline()) {
                char buf[16];
                int unified_ec = ECANCELED;
                for (int i = 0; i < _ndone; ++i) {
                    Controller* sub_cntl = &sub_done(i)->cntl;
                    const int ec = sub_cntl->ErrorCode();
                    if (ec != 0 && ec != ECANCELED) {
                        if (unified_ec == ECANCELED) {
                            unified_ec = ec;
                        } else if (unified_ec != ec) {
                            unified_ec = ETOOMANYFAILS;
                            break;
                        }
                    }
                }
                _cntl->SetFailed(unified_ec, "%d/%d channels failed, fail_limit=%d",
                                 nfailed, _ndone, _fail_limit);
                for (int i = 0; i < _ndone; ++i) {
                    Controller* sub_cntl = &sub_done(i)->cntl;
                    if (sub_cntl->FailedInline()) {
                        const int len = snprintf(buf, sizeof(buf), " [C%d]", i);
                        _cntl->_error_text.append(buf, len);
                        _cntl->_error_text.append(sub_cntl->_error_text);
                    }
                }
            }
        } else {
            // Failed sub channels does not reach the limit, the RPC is
            // considered to be successful. For example, a RPC to a
            // ParallelChannel is canceled by user however enough sub calls
            // (> _ndone - fail_limit) already succeed before the canceling,
            // the RPC is still successful rather than ECANCELED.
            _cntl->_error_code = 0;
            _cntl->_error_text.clear();
        }
        google::protobuf::Closure* user_done = _user_done;
        const CallId saved_cid = _cntl->call_id();
        // NOTE: we don't destroy self here, controller destroys this done in
        // Reset() so that user can access sub controllers before Reset().
        if (user_done) {
            _cntl->OnRPCEnd(butil::gettimeofday_us());
            user_done->Run();
        }
        CHECK_EQ(0, bthread_id_unlock_and_destroy(saved_cid));
    }

    int sub_done_size() const { return _ndone; }
    SubDone* sub_done(int i) { return &_sub_done[i]; }
    const SubDone* sub_done(int i) const { return &_sub_done[i]; }


    int& sub_done_map(int i) {
        return reinterpret_cast<int*>((_sub_done + _ndone))[i];
    }

    int sub_done_map(int i) const {
        return reinterpret_cast<const int*>((_sub_done + _ndone))[i];
    }
    
    int sub_channel_size() const { return _nchan; }
    // Different from sub_done(), sub_channel_controller returns NULL for
    // invalid accesses and never crashes.
    const Controller* sub_channel_controller(int i) const {
        if (i >= 0 && i < _nchan) {
            if (_nchan == _ndone) {
                return &sub_done(i)->cntl;
            }
            const int offset = sub_done_map(i);
            if (offset >= 0) {
                return &sub_done(offset)->cntl;
            }
        }
        return NULL;
    }

private:
    int _fail_limit;
    int _ndone;
    int _nchan;
#if defined(__clang__)
    int ALLOW_UNUSED _memsize;
#else
    int _memsize;
#endif
    butil::atomic<int> _current_fail;
    butil::atomic<uint32_t> _current_done;
    Controller* _cntl;
    google::protobuf::Closure* _user_done;
    bthread_t _callmethod_bthread;
    pthread_t _callmethod_pthread;
    SubDone _sub_done[0];
};

// Used in controller.cpp
void DestroyParallelChannelDone(google::protobuf::Closure* c) {
    ParallelChannelDone::Destroy(static_cast<ParallelChannelDone*>(c));
}

const Controller* GetSubControllerOfParallelChannel(
    const google::protobuf::Closure* c, int i) {
    const ParallelChannelDone* d = static_cast<const ParallelChannelDone*>(c);
    return d->sub_channel_controller(i);
}

int ParallelChannel::Init(const ParallelChannelOptions* options) {
    if (options != NULL) {
        _options = *options;
    }
    return 0;
}

int ParallelChannel::AddChannel(ChannelBase* sub_channel,
                                ChannelOwnership ownership,
                                CallMapper* call_mapper,
                                ResponseMerger* merger) {
    if (NULL == sub_channel) {
        LOG(ERROR) << "Param[sub_channel] is NULL";
        return -1;
    }
    if (_chans.capacity() == 0) {
        _chans.reserve(32);
    }
    SubChan sc;
    sc.chan = sub_channel;
    sc.ownership = ownership;
    sc.call_mapper = call_mapper;
    sc.merger = merger;
    _chans.push_back(sc);
    return 0;
}

struct SortByChannelPtr {
    bool operator()(const ParallelChannel::SubChan& c1,
                    const ParallelChannel::SubChan& c2) const {
        return c1.chan < c2.chan;
    }
};

struct EqualChannelPtr {
    bool operator()(const ParallelChannel::SubChan& c1,
                    const ParallelChannel::SubChan& c2) const {
        return c1.chan == c2.chan;
    }
};

void ParallelChannel::Reset() {
    // Removal of channels are a little complex because a channel may be
    // added multiple times.

    // Dereference call_mapper and mergers first.
    for (size_t i = 0; i < _chans.size(); ++i) {
        _chans[i].call_mapper.reset();
        _chans[i].merger.reset();
    }
    
    // Remove not own-ed channels.
    for (size_t i = 0; i < _chans.size();) {
        if (_chans[i].ownership != OWNS_CHANNEL) {
            _chans[i] = _chans.back();
            _chans.pop_back();
        } else {
            ++i;
        }
    }

    if (_chans.empty()) {
        return;
    }

    // Sort own-ed channels so that we can deduplicate them more efficiently.
    std::sort(_chans.begin(), _chans.end(), SortByChannelPtr());
    const size_t uniq_size =
        std::unique(_chans.begin(), _chans.end(), EqualChannelPtr())
        - _chans.begin();
    for (size_t i = 0; i < uniq_size; ++i) {
        CHECK_EQ(_chans[i].ownership, OWNS_CHANNEL);
        delete _chans[i].chan;
    }
    _chans.clear();
}

ParallelChannel::~ParallelChannel() {
    Reset();
}

static void HandleTimeout(void* arg) {
    bthread_id_t correlation_id = { (uint64_t)arg };
    bthread_id_error(correlation_id, ERPCTIMEDOUT);
}

void* ParallelChannel::RunDoneAndDestroy(void* arg) {
    Controller* c = static_cast<Controller*>(arg);
    // Move done out from the controller.
    google::protobuf::Closure* done = c->_done;
    c->_done = NULL;
    // Save call_id from the controller which may be deleted after Run().
    const bthread_id_t cid = c->call_id();
    done->Run();
    CHECK_EQ(0, bthread_id_unlock_and_destroy(cid));
    return NULL;
}

void ParallelChannel::CallMethod(
    const google::protobuf::MethodDescriptor* method,
    google::protobuf::RpcController* cntl_base,
    const google::protobuf::Message* request,
    google::protobuf::Message* response,
    google::protobuf::Closure* done) {
    Controller* cntl = static_cast<Controller*>(cntl_base);
    cntl->OnRPCBegin(butil::gettimeofday_us());
    // Make sure cntl->sub_count() always equal #sub-channels
    const int nchan = _chans.size();
    cntl->_pchan_sub_count = nchan;

    const CallId cid = cntl->call_id();
    const int rc = bthread_id_lock(cid, NULL);
    if (rc != 0) {
        CHECK_EQ(EINVAL, rc);
        if (!cntl->FailedInline()) {
            cntl->SetFailed(EINVAL, "Fail to lock call_id=%" PRId64, cid.value);
        }
        LOG_IF(ERROR, cntl->is_used_by_rpc())
            << "Controller=" << cntl << " was used by another RPC before. "
            "Did you forget to Reset() it before reuse?";
        // Have to run done in-place.
        // Read comment in CallMethod() in channel.cpp for details.
        if (done) {
            done->Run();
        }
        return;
    }
    cntl->set_used_by_rpc();

    ParallelChannelDone* d = NULL;
    int ndone = nchan;
    int fail_limit = 1;
    DEFINE_SMALL_ARRAY(SubCall, aps, nchan, 64);

    if (cntl->FailedInline()) {
        // The call_id is cancelled before RPC.
        goto FAIL;
    }
    // we don't support http whose response is NULL.
    if (response == NULL) {
        cntl->SetFailed(EINVAL, "response must be non-NULL");
        goto FAIL;
    }
    if (nchan == 0) {
        cntl->SetFailed(EPERM, "No channels added");
        goto FAIL;
    }

    for (int i = 0; i < nchan; ++i) {
        SubChan& sub_chan = _chans[i];
        if (sub_chan.call_mapper != NULL) {
            aps[i] = sub_chan.call_mapper->Map(i, method, request, response);
            // Test is_skip first because it implies is_bad.
            if (aps[i].is_skip()) {
                --ndone;
            } else if (aps[i].is_bad()) {
                cntl->SetFailed(
                    EREQUEST, "CallMapper of channel[%d] returns Bad()", i);
                goto FAIL;
            }
        } else {
            google::protobuf::Message* cur_res = response->New();
            if (cur_res == NULL) {
                cntl->SetFailed(ENOMEM, "Fail to new response");
                goto FAIL;
            }
            aps[i] = SubCall(method, request, cur_res, DELETE_RESPONSE);
        }
    }
    if (ndone <= 0) {
        cntl->SetFailed(ECANCELED, "Skipped all channels(%d)", nchan);
        goto FAIL;
    }

    if (_options.fail_limit < 0) {
        // Both Controller and ParallelChannel haven't set `fail_limit'
        fail_limit = ndone;
    } else {
        fail_limit = _options.fail_limit;
        if (fail_limit < 1) {
            fail_limit = 1;
        } else if (fail_limit > ndone) {
            fail_limit = ndone;
        }
    }
    
    d = ParallelChannelDone::Create(fail_limit, ndone, aps, nchan,
                                    cntl, done);
    if (NULL == d) {
        cntl->SetFailed(ENOMEM, "Fail to new ParallelChannelDone");
        goto FAIL;
    }

    for (int i = 0, j = 0; i < nchan; ++i) {
        SubChan& sub_chan = _chans[i];
        if (!aps[i].is_skip()) {
            ParallelChannelDone::SubDone* sd = d->sub_done(j++);
            sd->ap = aps[i];
            sd->shared_data = d;
            sd->merger = sub_chan.merger;
        }
    }
    cntl->_response = response;
    cntl->_done = d;
    cntl->add_flag(Controller::FLAGS_DESTROY_CID_IN_DONE);

    if (cntl->timeout_ms() == UNSET_MAGIC_NUM) {
        cntl->set_timeout_ms(_options.timeout_ms);
    }
    if (cntl->timeout_ms() >= 0) {
        cntl->_deadline_us = cntl->timeout_ms() * 1000L + cntl->_begin_time_us;
        // Setup timer for RPC timetout
        const int rc = bthread_timer_add(
            &cntl->_timeout_id,
            butil::microseconds_to_timespec(cntl->_deadline_us),
            HandleTimeout, (void*)cid.value);
        if (rc != 0) {
            cntl->SetFailed(rc, "Fail to add timer");
            goto FAIL;
        }
    } else {
        cntl->_deadline_us = -1;
    }
    d->SaveThreadInfoOfCallsite();
    CHECK_EQ(0, bthread_id_unlock(cid));
    // Don't touch `cntl' and `d' again (for async RPC)
    
    for (int i = 0, j = 0; i < nchan; ++i) {
        if (!aps[i].is_skip()) {
            ParallelChannelDone::SubDone* sd = d->sub_done(j++);
            // Forward the attachment to each sub call
            sd->cntl.request_attachment().append(cntl->request_attachment());
            _chans[i].chan->CallMethod(sd->ap.method, &sd->cntl,
                                       sd->ap.request, sd->ap.response, sd);
        }
        // Although we can delete request (if delete_request is true) after
        // starting sub call, we leave it in ~SubCall(called when d is
        // Destroy()-ed) because we may need to check requests for debugging
        // purposes.
    }
    if (done == NULL) {
        Join(cid);
        cntl->OnRPCEnd(butil::gettimeofday_us());
    }
    return;

FAIL:
    // The RPC was failed after locking call_id and before calling sub channels.
    if (d) {
        // Set the _done to NULL to make sure cntl->sub(any_index) is NULL.
        cntl->_done = NULL;
        ParallelChannelDone::Destroy(d);
    }
    if (done) {
        if (!cntl->is_done_allowed_to_run_in_place()) {
            bthread_t bh;
            bthread_attr_t attr = (FLAGS_usercode_in_pthread ?
                                   BTHREAD_ATTR_PTHREAD : BTHREAD_ATTR_NORMAL);
            // Hack: save done in cntl->_done to remove a malloc of args.
            cntl->_done = done;
            if (bthread_start_background(&bh, &attr, RunDoneAndDestroy, cntl) == 0) {
                return;
            }
            cntl->_done = NULL;
            LOG(FATAL) << "Fail to start bthread";
        }
        done->Run();
    }
    CHECK_EQ(0, bthread_id_unlock_and_destroy(cid));
}

int ParallelChannel::Weight() {
    if (_chans.empty()) {
        return 0;
    }
    int w = _chans[0].chan->Weight();
    for (size_t i = 1; i < _chans.size(); ++i) {
        const int w2 = _chans[i].chan->Weight();
        if (w2 < w) {
            w = w2;
        }
    }
    return w;
}

int ParallelChannel::CheckHealth() {
    if (_chans.empty()) {
        return -1;
    }
    int threshold = (int)_chans.size();
    if (_options.fail_limit > 0) {
        threshold -= _options.fail_limit;
        ++threshold;
    }
    if (threshold <= 0) {
        return 0;
    }
    int nhealthy = 0;
    for (size_t i = 0; i < _chans.size(); ++i) {
        nhealthy += (_chans[i].chan->CheckHealth() == 0);
        if (nhealthy >= threshold) {
            return 0;
        }
    }
    return -1;
}

void ParallelChannel::Describe(
    std::ostream& os, const DescribeOptions& options) const {
    os << "ParallelChannel[";
    if (!options.verbose) {
        os << _chans.size();
    } else {
        for (size_t i = 0; i < _chans.size(); ++i) {
            if (i != 0) {
                os << ' ';
            }
            os << *_chans[i].chan;
        }
    }
    os << "]";
}

} // namespace brpc
