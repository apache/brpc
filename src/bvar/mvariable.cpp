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

// Date: 2021/11/17 14:37:53

#include <utility>
#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>
#include "butil/logging.h"                       // LOG
#include "butil/errno.h"                         // berror
#include "butil/containers/flat_map.h"           // butil::FlatMap
#include "butil/memory/scope_guard.h"
#include "butil/scoped_lock.h"                   // BAIDU_SCOPE_LOCK
#include "butil/file_util.h"                     // butil::FilePath
#include "butil/reloadable_flags.h"
#include "bvar/variable.h"
#include "bvar/mvariable.h"

namespace bvar {

DECLARE_bool(bvar_abort_on_same_name);

extern bool s_bvar_may_abort;

static bool validator_bvar_max_multi_dimension_metric_number(const char*, int32_t v) {
    if (v < 1) {
        LOG(ERROR) << "Invalid bvar_max_multi_dimension_metric_number=" << v;
        return false;
    }
    return true;
}

DEFINE_int32(bvar_max_multi_dimension_metric_number, 1024, "Max number of multi dimension");
BUTIL_VALIDATE_GFLAG(bvar_max_multi_dimension_metric_number,
                     validator_bvar_max_multi_dimension_metric_number);

static bool validator_bvar_max_dump_multi_dimension_metric_number(const char*, int32_t v) {
    if (v < 0) {
        LOG(ERROR) << "Invalid bvar_max_dump_multi_dimension_metric_number=" << v;
        return false;
    }
    return true;
}
DEFINE_int32(bvar_max_dump_multi_dimension_metric_number, 1024,
             "Max number of multi dimension metric number to dump by prometheus rpc service");
BUTIL_VALIDATE_GFLAG(bvar_max_dump_multi_dimension_metric_number,
                     validator_bvar_max_dump_multi_dimension_metric_number);

static bool validator_max_multi_dimension_stats_count(const char*, uint32_t v) {
    if (v < 1) {
        LOG(ERROR) << "Invalid max_multi_dimension_stats_count=" << v;
        return false;
    }
    return true;
}
DEFINE_uint32(max_multi_dimension_stats_count, 20000, "Max stats count of a multi dimension metric.");
BUTIL_VALIDATE_GFLAG(max_multi_dimension_stats_count,
                     validator_max_multi_dimension_stats_count);

struct MVarEntry {
    MVariableBase::SharedExposedRef ref;
};

typedef butil::FlatMap<std::string, MVarEntry> MVarMap;

struct MVarMapWithLock : public MVarMap {
    pthread_mutex_t mutex;

    MVarMapWithLock() {
        if (init(256) != 0) {
            LOG(WARNING) << "Fail to init";
        }
        pthread_mutex_init(&mutex, nullptr);
    }
};

// We have to initialize global map on need because bvar is possibly used
// before main().
static pthread_once_t s_mvar_map_once = PTHREAD_ONCE_INIT;
static MVarMapWithLock* s_mvar_map = nullptr;

static void init_mvar_map() {
    // It's probably slow to initialize all sub maps, but rpc often expose 
    // variables before user. So this should not be an issue to users.
    s_mvar_map = new MVarMapWithLock();
}

inline MVarMapWithLock& get_mvar_map() {
    pthread_once(&s_mvar_map_once, init_mvar_map);
    return *s_mvar_map;
}

MVariableBase::~MVariableBase() {
    CHECK(!hide()) << "Subclass of MVariableBase MUST call hide() manually in their "
                      "dtors to avoid displaying a variable that is just destructing";
}

std::string MVariableBase::get_description() {
    std::ostringstream os;
    describe(os);
    return os.str();
}

int MVariableBase::describe_exposed(const std::string& name,
                                std::ostream& os) {
    MVarMapWithLock& m = get_mvar_map();
    MVariableBase* var = nullptr;
    SharedExposedRef ref;
    {
        BAIDU_SCOPED_LOCK(m.mutex);
        MVarEntry* entry = m.seek(name);
        if (entry == nullptr) {
            return -1;
        }
        ref = entry->ref;
        var = ref->acquire();
    }
    if (var == nullptr) {
        return -1;
    }
    // Call describe() outside the MVarMap lock to avoid deadlock when the user
    // callback (e.g. Dumper) yields the bthread.
    var->describe(os);
    ref->release();
    return 0;
}

std::string MVariableBase::describe_exposed(const std::string& name) {
    std::ostringstream oss;
    if (describe_exposed(name, oss) == 0) {
        return oss.str();
    }
    return std::string();
}

int MVariableBase::expose_impl(const butil::StringPiece& prefix,
                               const butil::StringPiece& name) {
    if (name.empty()) {
        LOG(ERROR) << "Parameter[name] is empty";
        return -1;
    }
    // NOTE: It's impossible to atomically erase from a submap and insert into
    // another submap without a global lock. When the to-be-exposed name
    // already exists, there's a chance that we can't insert back previous
    // name. But it should be fine generally because users are unlikely to
    // expose a variable more than once and calls to expose() are unlikely
    // to contend heavily.

    // Remove previous exposure if needed (hide() waits for in-flight readers
    // and invalidates `_ref`).
    // Always start the new exposure with a fresh `_ref`,  because a previous
    // hide() may have permanently hidden the old `_ref`.
    hide();
    _ref = detail::make_exposed_ref(this);
    
    // Build the name.
    _name.clear();
    _name.reserve((prefix.size() + name.size()) * 5 / 4);
    if (!prefix.empty()) {
        to_underscored_name(&_name, prefix);
        if (!_name.empty() && butil::back_char(_name) != '_') {
            _name.push_back('_');
        }     
    }
    to_underscored_name(&_name, name);

    bool expose_succeeded = false;
    BUTIL_SCOPE_EXIT {
        if (!expose_succeeded) {
            _name.clear();
        }
    };
   
    if (count_exposed() > (size_t)FLAGS_bvar_max_multi_dimension_metric_number) {
        LOG(ERROR) << "Too many metric seen, overflow detected, max metric count:"
                   << FLAGS_bvar_max_multi_dimension_metric_number;
        return -1;
    }

    MVarMapWithLock& m = get_mvar_map();
    {
        BAIDU_SCOPED_LOCK(m.mutex);
        MVarEntry* entry = m.seek(_name);
        if (entry == nullptr) {
            entry = &m[_name];
            entry->ref = _ref;
            expose_succeeded = true;
            return 0;
        }
    }

    RELEASE_ASSERT_VERBOSE(!FLAGS_bvar_abort_on_same_name,
                           "Abort due to name conflict");
    if (!s_bvar_may_abort) {
        // Mark name conflict occurs, If this conflict happens before
        // initialization of bvar_abort_on_same_name, the validator will
        // abort the program if needed.
        s_bvar_may_abort = true;
    }

    LOG(WARNING) << "Already exposed `" << _name << "' whose describe is`"
                 << get_description() << "'";
    return -1;
}

bool MVariableBase::hide() {
    if (_name.empty()) {
        return false;
    }
    MVarMapWithLock& m = get_mvar_map();
    {
        BAIDU_SCOPED_LOCK(m.mutex);
        MVarEntry* entry = m.seek(_name);
        if (entry) {
            CHECK_EQ(1UL, m.erase(_name));
        } else {
            CHECK(false) << "`" << _name << "' must exist";
        }
    }
    // Remove previous exposure if needed (hide() waits for in-flight readers
    // and invalidates `_ref`).
    // Always start the new exposure with a fresh `_ref`,  because a previous
    // hide() may have permanently hidden the old `_ref`.
    if (_ref != nullptr) {
        _ref->hide_and_wait();
    }

    _name.clear();
    return true;
}

#ifdef UNIT_TEST
void MVariableBase::hide_all() {
    struct MVariableToHide {
        MVariableBase* variable;
        SharedExposedRef ref;
    };

    std::vector<MVariableToHide> variables;
    MVarMapWithLock& m = get_mvar_map();
    // Snapshot the owners while detaching the map entries. Waiting for readers
    // must happen outside the MVarMap lock.
    {
        BAIDU_SCOPED_LOCK(m.mutex);
        variables.reserve(m.size());
        for (MVarMap::const_iterator it = m.begin(); it != m.end(); ++it) {
            MVariableToHide variable;
            variable.ref = it->second.ref;
            variable.variable = variable.ref->acquire();
            if (variable.variable != nullptr) {
                variables.push_back(std::move(variable));
            }
        }
        m.clear();
    }

    for (auto& variable : variables) {
        // Touch the owner while the reference taken by acquire() above is still
        // held: release() lets a concurrent destructor run to completion, and
        // hide_and_wait() blocks until the last reference is gone, so it has to
        // come after release() rather than before it.
        variable.variable->_name.clear();
        variable.ref->release();
        variable.ref->hide_and_wait();
    }
}
#endif // end UNIT_TEST

size_t MVariableBase::count_exposed() {
    MVarMapWithLock& m = get_mvar_map();
    BAIDU_SCOPED_LOCK(m.mutex);
    return m.size();
}

void MVariableBase::list_exposed(std::vector<std::string>* names) {
    if (names == nullptr) {
        return;
    }

    names->clear();

    MVarMapWithLock& mvar_map = get_mvar_map();
    BAIDU_SCOPED_LOCK(mvar_map.mutex);
    names->reserve(mvar_map.size());
    for (MVarMap::const_iterator it = mvar_map.begin(); it != mvar_map.end(); ++it) {
        names->push_back(it->first);
    }
}

// Enforces FLAGS_bvar_max_dump_multi_dimension_metric_number over the whole
// dump rather than only in between two mvars. A composite metric writes a
// dozen lines or more per label set, so a single MultiDimension<Histogram>
// holding the label sets max_multi_dimension_stats_count allows is already
// hundreds of thousands of lines, all of which reached the output before
// anyone got to look at the count.
class LimitedDumper : public Dumper {
public:
    LimitedDumper(Dumper* dumper, size_t max_metric_count)
        : _dumper(dumper), _left(max_metric_count), _truncated(false) {}

    bool dump(const std::string& name, const butil::StringPiece& desc) override {
        return take_one() && _dumper->dump(name, desc);
    }
    bool dump_mvar(const std::string& name, const butil::StringPiece& desc) override {
        return take_one() && _dumper->dump_mvar(name, desc);
    }
    // A comment is not a metric of its own, but one that no sample may follow
    // would be left dangling.
    bool dump_comment(const std::string& name, const std::string& type) override {
        if (_left == 0) {
            _truncated = true;
            return false;
        }
        return _dumper->dump_comment(name, type);
    }

    // True once the budget is gone, whether or not anything was refused yet.
    bool exhausted() const { return _left == 0; }
    // True if something was actually refused.
    bool truncated() const { return _truncated; }

private:
    bool take_one() {
        if (_left == 0) {
            _truncated = true;
            return false;
        }
        --_left;
        return true;
    }

    Dumper* _dumper;
    size_t _left;
    bool _truncated;
};

int MVariableBase::dump_exposed(Dumper* dumper, const DumpOptions* options) {
    if (nullptr == dumper) {
        LOG(ERROR) << "Parameter[dumper] is nullptr";
        return -1;
    }
    DumpOptions opt;
    if (options) {
        opt = *options;
    }
    std::vector<std::string> mvars;
    list_exposed(&mvars);
    // The validator of the flag keeps it non negative.
    LimitedDumper limited_dumper(
        dumper, static_cast<size_t>(FLAGS_bvar_max_dump_multi_dimension_metric_number));
    size_t n = 0;
    for (size_t i = 0; i < mvars.size(); ++i) {
        MVariableBase* var = nullptr;
        SharedExposedRef ref;
        {
            MVarMapWithLock& m = get_mvar_map();
            BAIDU_SCOPED_LOCK(m.mutex);
            MVarEntry* entry = m.seek(mvars[i]);
            if (entry) {
                ref = entry->ref;
                var = ref->acquire();
            }
        }
        if (var != nullptr) {
            // Call dump() outside the MVarMap lock to avoid deadlock when the dump()
            // yields the bthread.
            n += var->dump(&limited_dumper, &opt);
            ref->release();
        }
        if (limited_dumper.exhausted()) {
            // An exact fit on the last mvar has lost nothing.
            if (limited_dumper.truncated() || i + 1 < mvars.size()) {
                LOG(WARNING) << "truncated because of exceed max dump multi dimension label number["
                             << FLAGS_bvar_max_dump_multi_dimension_metric_number << "]";
            }
            break;
        }
    }
    return n;
}

} // namespace bvar
