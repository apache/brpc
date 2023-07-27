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

#include <gflags/gflags.h>
#include <gflags/gflags_declare.h>
#include "butil/logging.h"                       // LOG
#include "butil/errno.h"                         // berror
#include "butil/containers/flat_map.h"           // butil::FlatMap
#include "butil/scoped_lock.h"                   // BAIDU_SCOPE_LOCK
#include "butil/file_util.h"                     // butil::FilePath
#include "bvar/variable.h"
#include "bvar/mvariable.h"

namespace bvar {

constexpr uint64_t MAX_LABELS_COUNT = 10;

DECLARE_bool(bvar_abort_on_same_name);

extern bool s_bvar_may_abort;

DEFINE_int32(bvar_max_multi_dimension_metric_number, 1024, "Max number of multi dimension");
DEFINE_int32(bvar_max_dump_multi_dimension_metric_number, 1024,
    "Max number of multi dimension metric number to dump by prometheus rpc service");

static bool validator_bvar_max_multi_dimension_metric_number(const char*, int32_t v) {
    if (v < 1) {
        LOG(ERROR) << "Invalid bvar_max_multi_dimension_metric_number=" << v;
        return false;
    }
    return true;
}

static bool validator_bvar_max_dump_multi_dimension_metric_number(const char*, int32_t v) {
    if (v < 0) {
        LOG(ERROR) << "Invalid bvar_max_dump_multi_dimension_metric_number=" << v;
        return false;
    }
    return true;
}


const bool ALLOW_UNUSED dummp_bvar_max_multi_dimension_metric_number = ::GFLAGS_NS::RegisterFlagValidator(
    &FLAGS_bvar_max_multi_dimension_metric_number, validator_bvar_max_multi_dimension_metric_number);

const bool ALLOW_UNUSED dummp_bvar_max_dump_multi_dimension_metric_number = ::GFLAGS_NS::RegisterFlagValidator(
  &FLAGS_bvar_max_dump_multi_dimension_metric_number, validator_bvar_max_dump_multi_dimension_metric_number);

class MVarEntry {
public:
    MVarEntry() : var(NULL) {}

    MVariable* var;
};

typedef butil::FlatMap<std::string, MVarEntry> MVarMap;

struct MVarMapWithLock : public MVarMap {
    pthread_mutex_t mutex;

    MVarMapWithLock() {
        CHECK_EQ(0, init(256, 80));
        pthread_mutex_init(&mutex, NULL);
    }
};

// We have to initialize global map on need because bvar is possibly used
// before main().
static pthread_once_t s_mvar_map_once = PTHREAD_ONCE_INIT;
static MVarMapWithLock* s_mvar_map = NULL;

static void init_mvar_map() {
    // It's probably slow to initialize all sub maps, but rpc often expose 
    // variables before user. So this should not be an issue to users.
    s_mvar_map = new MVarMapWithLock();
}

inline MVarMapWithLock& get_mvar_map() {
    pthread_once(&s_mvar_map_once, init_mvar_map);
    return *s_mvar_map;
}

MVariable::MVariable(const std::list<std::string>& labels) {
    _labels.assign(labels.begin(), labels.end());
    size_t n = labels.size();
    if (n > MAX_LABELS_COUNT) {
        LOG(ERROR) << "Too many labels: " << n << " seen, overflow detected, max labels count: " << MAX_LABELS_COUNT;
        _labels.resize(MAX_LABELS_COUNT);
    }
}

MVariable::~MVariable() {
    CHECK(!hide()) << "Subclass of MVariable MUST call hide() manually in their"
    " dtors to avoid displaying a variable that is just destructing";
}

std::string MVariable::get_description() {
    std::ostringstream os;
    describe(os);
    return os.str();
}

int MVariable::describe_exposed(const std::string& name,
                                std::ostream& os) {
    MVarMapWithLock& m = get_mvar_map();
    BAIDU_SCOPED_LOCK(m.mutex);
    MVarEntry* entry = m.seek(name);
    if (entry == NULL) {
        return -1;
    }
    entry->var->describe(os);
    return 0;
}

std::string MVariable::describe_exposed(const std::string& name) {
    std::ostringstream oss;
    if (describe_exposed(name, oss) == 0) {
        return oss.str();
    }
    return std::string();
}

int MVariable::expose_impl(const butil::StringPiece& prefix,
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

    // remove previous pointer from the map if needed.
    hide();
    
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
   
    if (count_exposed() > (size_t)FLAGS_bvar_max_multi_dimension_metric_number) {
        LOG(ERROR) << "Too many metric seen, overflow detected, max metric count:" << FLAGS_bvar_max_multi_dimension_metric_number;
        return -1;
    }

    MVarMapWithLock& m = get_mvar_map();
    {
        BAIDU_SCOPED_LOCK(m.mutex);
        MVarEntry* entry = m.seek(_name);
        if (entry == NULL) {
            entry = &m[_name];
            entry->var = this;
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
    _name.clear();
    return 0;
}

bool MVariable::hide() {
    if (_name.empty()) {
        return false;
    }

    MVarMapWithLock& m = get_mvar_map();
    BAIDU_SCOPED_LOCK(m.mutex);
    MVarEntry* entry = m.seek(_name);
    if (entry) {
        CHECK_EQ(1UL, m.erase(_name));
    } else {
        CHECK(false) << "`" << _name << "' must exist";
    }
    _name.clear();
    return true;
}

#ifdef UNIT_TEST
void MVariable::hide_all() {
    MVarMapWithLock& m = get_mvar_map();
    BAIDU_SCOPED_LOCK(m.mutex);
    m.clear();
}
#endif // end UNIT_TEST

size_t MVariable::count_exposed() {
    MVarMapWithLock& m = get_mvar_map();
    BAIDU_SCOPED_LOCK(m.mutex);
    return m.size();
}

void MVariable::list_exposed(std::vector<std::string>* names) {
    if (names == NULL) {
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

size_t MVariable::dump_exposed(Dumper* dumper, const DumpOptions* options) {
    if (NULL == dumper) {
        LOG(ERROR) << "Parameter[dumper] is NULL";
        return -1;
    }
    DumpOptions opt;
    if (options) {
        opt = *options;
    }
    std::vector<std::string> mvars;
    list_exposed(&mvars);
    size_t n = 0;
    for (auto& mvar : mvars) {
        MVarMapWithLock& m = get_mvar_map();
        BAIDU_SCOPED_LOCK(m.mutex);
        MVarEntry* entry = m.seek(mvar);
        if (entry) {
            n += entry->var->dump(dumper, &opt);
        }
	if (n > static_cast<size_t>(FLAGS_bvar_max_dump_multi_dimension_metric_number)) {
            LOG(WARNING) << "truncated because of \
		            exceed max dump multi dimension label number["
			 << FLAGS_bvar_max_dump_multi_dimension_metric_number
			 << "]";
            break;
	}
    }
    return n;
}

} // namespace bvar
