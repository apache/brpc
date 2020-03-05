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


#include "butil/containers/flat_map.h"
#include "brpc/log.h"
#include "brpc/load_balancer.h"
#include "brpc/details/naming_service_thread.h"
#include "brpc/partition_channel.h"
#include "brpc/global.h"


namespace brpc {

// ================= PartitionChannelBase ====================

// Base of PartitionChannel and DynamicPartitionChannel.
class PartitionChannelBase : public ParallelChannel,
                             public NamingServiceWatcher {
public:
    PartitionChannelBase();
    ~PartitionChannelBase();

    int Init(int num_partition_kinds,
             PartitionParser* partition_parser,
             const char* load_balancer_name,
             const PartitionChannelOptions* options);

    int partition_count() const { return channel_count(); }

    size_t AddServersInBatch(const std::vector<ServerId>& servers);
    size_t RemoveServersInBatch(const std::vector<ServerId>& servers);

private:
    bool initialized() const { return _parser != NULL; }
    void PartitionServersIntoTemps(const std::vector<ServerId>& servers);
    void OnAddedServers(const std::vector<ServerId>& servers);
    void OnRemovedServers(const std::vector<ServerId>& servers);

    struct SubChannel : public Channel {
        SharedLoadBalancer* lb() { return _lb.get(); }
        std::vector<ServerId> tmp;
    };

    SubChannel* _subs;
    PartitionParser* _parser;
};

PartitionChannelBase::PartitionChannelBase()
    : _subs(NULL)
    , _parser(NULL) {
}

PartitionChannelBase::~PartitionChannelBase() {
    delete [] _subs;
    _subs = NULL;
}

int PartitionChannelBase::Init(int num_partition_kinds,
                               PartitionParser* partition_parser,
                               const char* load_balancer_name,
                               const PartitionChannelOptions* options_in) {
    if (num_partition_kinds <= 0) {
        LOG(ERROR) << "Parameter[num_partition_kinds] must be positive";
        return -1;
    }
    if (NULL == partition_parser) {
        LOG(ERROR) << "Parameter[partition_parser] must be non-NULL";
        return -1;
    }
    PartitionChannelOptions options;
    if (options_in) {
        options = *options_in;
    }
    options.succeed_without_server = true;
    options.log_succeed_without_server = false;
    _subs = new (std::nothrow) SubChannel[num_partition_kinds];
    if (NULL == _subs) {
        LOG(ERROR) << "Fail to new Channels[" << num_partition_kinds << "]";
        return -1;
    }
    for (int i = 0; i < num_partition_kinds; ++i) {
        if (_subs[i].Init("list://", load_balancer_name, &options) != 0) {
            LOG(ERROR) << "Fail to init sub channel[" << i << "]";
            return -1;
        }
    }
    for (int i = 0; i < num_partition_kinds; ++i) {
        if (AddChannel(&_subs[i], DOESNT_OWN_CHANNEL,
                       options.call_mapper.get(),
                       options.response_merger.get()) != 0) {
            LOG(ERROR) << "Fail to add sub channel[" << i << "]";
            return -1;
        }
    }
    ParallelChannelOptions pchan_options;
    pchan_options.timeout_ms = options.timeout_ms;
    pchan_options.fail_limit = options.fail_limit;
    if (ParallelChannel::Init(&pchan_options) != 0) {
        LOG(ERROR) << "Fail to init PartitionChannel as ParallelChannel";
        return -1;
    }
    // Must be last one because it's the marker of initialized().
    _parser = partition_parser;
    return 0;
}
    
void PartitionChannelBase::PartitionServersIntoTemps(
    const std::vector<ServerId>& servers) {
    for (int i = 0; i < partition_count(); ++i) {
        _subs[i].tmp.clear();
    }
    for (size_t i = 0; i < servers.size(); ++i) {
        Partition part;
        if (!_parser->ParseFromTag(servers[i].tag, &part)) {
            LOG(ERROR) << "Fail to parse " << servers[i].tag;
            continue;
        }
        if (part.num_partition_kinds != partition_count()) {
            // Not belonging to this channel.
            continue;
        }
        if (part.index < 0 || part.index >= partition_count()) {
            LOG(ERROR) << "Invalid index=" << part.index << " in tag=`"
                       << servers[i].tag << "'";
            continue;
        }
        if (_subs[part.index].tmp.capacity() == 0) {
            _subs[part.index].tmp.reserve(16);
        }
        _subs[part.index].tmp.push_back(servers[i]);
    }
}

size_t PartitionChannelBase::AddServersInBatch(
    const std::vector<ServerId>& servers) {
    PartitionServersIntoTemps(servers);
    size_t ntotal = 0;
    for (int i = 0; i < partition_count(); ++i) {
        if (!_subs[i].tmp.empty()) {
            size_t n = _subs[i].lb()->AddServersInBatch(_subs[i].tmp);
            ntotal += n;
            RPC_VLOG << "Added " << n << " servers to channel[" << i << "]";
        }
    }
    return ntotal;
}
    
size_t PartitionChannelBase::RemoveServersInBatch(
    const std::vector<ServerId>& servers) {
    PartitionServersIntoTemps(servers);
    size_t ntotal = 0;
    for (int i = 0; i < partition_count(); ++i) {
        if (!_subs[i].tmp.empty()) {
            size_t n = _subs[i].lb()->RemoveServersInBatch(_subs[i].tmp);
            ntotal += n;
            RPC_VLOG << "Removed " << n << " servers from channel[" << i << "]";
        }
    }
    return ntotal;
}

void PartitionChannelBase::OnAddedServers(
    const std::vector<ServerId>& servers) {
    AddServersInBatch(servers);
}

void PartitionChannelBase::OnRemovedServers(
    const std::vector<ServerId>& servers) {
    RemoveServersInBatch(servers);
}

// ================= PartitionChannel ====================

PartitionChannelOptions::PartitionChannelOptions()
    : ChannelOptions(), fail_limit(-1) {
}

PartitionChannel::PartitionChannel()
    : _pchan(NULL)
    , _parser(NULL) {
}

PartitionChannel::~PartitionChannel() {
    if (_nsthread_ptr) {
        if (_pchan) {
            _nsthread_ptr->RemoveWatcher(_pchan);
        }
        _nsthread_ptr.reset();
    }
    delete _pchan;
    _pchan = NULL;
    delete _parser;
    _parser = NULL;
}

int PartitionChannel::Init(int num_partition_kinds,
                           PartitionParser* partition_parser,
                           const char* ns_url, 
                           const char* load_balancer_name,
                           const PartitionChannelOptions* options_in) {
    // Force naming services to register.
    GlobalInitializeOrDie();
    if (num_partition_kinds == 0) {
        LOG(ERROR) << "Parameter[num_partition_kinds] must be positive";
        return -1;
    }
    if (NULL == partition_parser) {
        LOG(ERROR) << "Parameter[partition_parser] must be non-NULL";
        return -1;
    }
    GetNamingServiceThreadOptions ns_opt;
    if (options_in) {
        ns_opt.succeed_without_server = options_in->succeed_without_server;
    }
    if (GetNamingServiceThread(&_nsthread_ptr, ns_url, &ns_opt) != 0) {
        LOG(ERROR) << "Fail to get NamingServiceThread";
        return -1;
    }
    _pchan = new (std::nothrow) PartitionChannelBase;
    if (NULL == _pchan) {
        LOG(ERROR) << "Fail to new PartitionChannelBase";
        return -1;
    }
    if (_pchan->Init(num_partition_kinds, partition_parser,
                     load_balancer_name, options_in) != 0) {
        LOG(ERROR) << "Fail to init PartitionChannelBase";
        return -1;
    }
    if (_nsthread_ptr->AddWatcher(
            _pchan, (options_in ?   options_in->ns_filter : NULL)) != 0) {
        LOG(ERROR) << "Fail to add PartitionChannelBase as watcher";
        return -1;
    }
    // Must be last one because it's the marker of initialized().
    _parser = partition_parser;
    return 0;
}

void PartitionChannel::CallMethod(
    const google::protobuf::MethodDescriptor* method,
    google::protobuf::RpcController* controller,
    const google::protobuf::Message* request,
    google::protobuf::Message* response,
    google::protobuf::Closure* done) {
    if (_pchan != NULL) {
        _pchan->CallMethod(method, controller, request, response, done);
    } else {
        Controller* cntl = static_cast<Controller*>(controller);
        cntl->SetFailed(EINVAL, "PartitionChannel=%p is not initialized yet",
                        this);
        // This is a branch only entered by wrongly-used RPC, just call done
        // in-place. See comments in channel.cpp on deadlock concerns.
        if (done) {
            done->Run();
        }
    }
}

int PartitionChannel::partition_count() const {
    return _pchan ? _pchan->partition_count() : 0;
}

int PartitionChannel::CheckHealth() {
    if (_pchan == NULL) {
        return -1;
    }
    return static_cast<ChannelBase*>(_pchan)->CheckHealth();
}

// ================= DynamicPartitionChannel ====================

class DynamicPartitionChannel::Partitioner : public NamingServiceWatcher {
public:
    void PartitionServersIntoTemps(const std::vector<ServerId>& servers) {
        for (PartChanMap::const_iterator it = _part_chan_map.begin();
             it != _part_chan_map.end(); ++it) {
            it->second->tmp.clear();
        }
        for (size_t i = 0; i < servers.size(); ++i) {
            Partition part;
            if (!_parser->ParseFromTag(servers[i].tag, &part)) {
                LOG(ERROR) << "Fail to parse " << servers[i].tag;
                continue;
            }
            if (part.num_partition_kinds <= 0) {
                LOG(ERROR) << "Invalid num_partition_kinds=" << part.num_partition_kinds
                           << " in tag=`" << servers[i].tag << "'";
                continue;
            }
            if (part.index < 0 || part.index >= part.num_partition_kinds) {
                LOG(ERROR) << "Invalid index=" << part.index << " in tag=`"
                           << servers[i].tag << "'";
                continue;
            }
            SubPartitionChannel** ppchan = _part_chan_map.seek(part.num_partition_kinds);
            SubPartitionChannel* pchan = NULL;
            if (ppchan == NULL) {
                pchan = new (std::nothrow) SubPartitionChannel;
                if (pchan == NULL) {
                    LOG(ERROR) << "Fail to new SubPartitionChannel";
                    continue;
                }
                if (pchan->Init(part.num_partition_kinds, _parser,
                                _load_balancer_name.c_str(), &_options) != 0) {
                    LOG(ERROR) << "Fail to init SubPartitionChannel=#"
                               << part.num_partition_kinds;
                    delete pchan;
                    continue;
                }
                if (_schan->AddChannel(pchan, &pchan->handle) != 0) {
                    LOG(ERROR) << "Fail to add SubPartitionChannel=#"
                               << part.num_partition_kinds;
                    delete pchan;
                    continue;
                }
                _part_chan_map[part.num_partition_kinds] = pchan;
                RPC_VLOG << "Added partition=" << part.num_partition_kinds;
            } else {
                pchan = *ppchan;
                CHECK_EQ(part.num_partition_kinds, pchan->partition_count());
            }
            
            if (pchan->tmp.capacity() == 0) {
                pchan->tmp.reserve(16);
            }
            pchan->tmp.push_back(servers[i]);
        }
    }

    void OnAddedServers(const std::vector<ServerId>& servers) {
        PartitionServersIntoTemps(servers);
        for (PartChanMap::const_iterator it = _part_chan_map.begin();
             it != _part_chan_map.end(); ++it) {
            if (!it->second->tmp.empty()) {
                size_t n = it->second->AddServersInBatch(it->second->tmp);
                it->second->num_servers += n;
                RPC_VLOG << "Added " << n << " servers to partition="
                         << it->first;
            }
        }
    }
    
    void OnRemovedServers(const std::vector<ServerId>& servers) {
        PartitionServersIntoTemps(servers);
        std::vector<int> erased_parts;
        for (PartChanMap::const_iterator it = _part_chan_map.begin();
             it != _part_chan_map.end(); ++it) {
            SubPartitionChannel* partchan = it->second;
            if (!partchan->tmp.empty()) {
                size_t n = partchan->RemoveServersInBatch(partchan->tmp);
                partchan->num_servers -= n;
                RPC_VLOG << "Removed " << n << " servers from partition="
                         << it->first;
                if (partchan->num_servers <= 0) {
                    CHECK_EQ(0, partchan->num_servers);
                    const int npart = partchan->partition_count();
                    _schan->RemoveAndDestroyChannel(partchan->handle);
                    // NOTE: Don't touch partchan again!
                    RPC_VLOG << "Removed partition=" << npart;
                    erased_parts.push_back(it->first);
                }
            }
        }
        for (size_t i = 0; i < erased_parts.size(); ++i) {
            CHECK_EQ(1UL, _part_chan_map.erase(erased_parts[i]));
        }
    }

    Partitioner()
        : _schan(NULL)
        , _parser(NULL)
    {}

    ~Partitioner() {
        // Do nothing. _schan deletes all sub channels.
    }
    
    int Init(SelectiveChannel* schan,
             PartitionParser* parser,
             const char* load_balancer_name,
             const PartitionChannelOptions* options) {
        _schan = schan;
        _parser = parser;
        _load_balancer_name = load_balancer_name;
        if (options) {
            _options = *options;
        }
        if (_part_chan_map.init(32, 70) != 0) {
            LOG(ERROR) << "Fail to init _part_chan_map";
            return -1;
        }
        return 0;
    }

private:
    struct SubPartitionChannel : public PartitionChannelBase {
        SubPartitionChannel() : num_servers(0) {}
        int num_servers;
        SelectiveChannel::ChannelHandle handle;  // uninitialized
        std::vector<ServerId> tmp;
    };
    typedef butil::FlatMap<int, SubPartitionChannel*> PartChanMap;
    
    PartChanMap _part_chan_map;
    SelectiveChannel* _schan;
    PartitionParser* _parser;
    std::string _load_balancer_name;
    PartitionChannelOptions _options;
};

DynamicPartitionChannel::DynamicPartitionChannel()
    : _partitioner(NULL)
    , _parser(NULL) {
}

DynamicPartitionChannel::~DynamicPartitionChannel() {
    if (_nsthread_ptr) {
        if (_partitioner) {
            _nsthread_ptr->RemoveWatcher(_partitioner);
        }
        _nsthread_ptr.reset();
    }
    delete _partitioner;
    _partitioner = NULL;
    delete _parser;
    _parser = NULL;
}

int DynamicPartitionChannel::Init(
    PartitionParser* partition_parser,
    const char* ns_url, 
    const char* load_balancer_name,
    const PartitionChannelOptions* options_in) {
    GlobalInitializeOrDie();
    if (NULL == partition_parser) {
        LOG(ERROR) << "Parameter[partition_parser] must be non-NULL";
        return -1;
    }
    GetNamingServiceThreadOptions ns_opt;
    if (options_in) {
        ns_opt.succeed_without_server = options_in->succeed_without_server;
    }
    if (GetNamingServiceThread(&_nsthread_ptr, ns_url, &ns_opt) != 0) {
        LOG(ERROR) << "Fail to get NamingServiceThread";
        return -1;
    }
    if (_schan.Init("_dynpart", options_in) != 0) {
        LOG(ERROR) << "Fail to init _schan";
        return -1;
    }
    _partitioner = new (std::nothrow) Partitioner;
    if (NULL == _partitioner) {
        LOG(ERROR) << "Fail to new Partitioner";
        return -1;
    }
    if (_partitioner->Init(&_schan, partition_parser,
                           load_balancer_name, options_in) != 0) {
        LOG(ERROR) << "Fail to init Partitioner";
        return -1;
    }
    if (_nsthread_ptr->AddWatcher(
            _partitioner, (options_in ? options_in->ns_filter : NULL)) != 0) {
        LOG(ERROR) << "Fail to add Partitioner as watcher";
        return -1;
    }
    // Must be last one because it's the marker of initialized().
    _parser = partition_parser;
    return 0;
}

void DynamicPartitionChannel::CallMethod(
    const google::protobuf::MethodDescriptor* method,
    google::protobuf::RpcController* controller,
    const google::protobuf::Message* request,
    google::protobuf::Message* response,
    google::protobuf::Closure* done) {
    _schan.CallMethod(method, controller, request, response, done);
}

} // namespace brpc
