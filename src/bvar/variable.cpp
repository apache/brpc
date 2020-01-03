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

// Date: 2014/09/22 19:04:47

#include <pthread.h>
#include <set>                                  // std::set
#include <fstream>                              // std::ifstream
#include <sstream>                              // std::ostringstream
#include <gflags/gflags.h>
#include "butil/macros.h"                        // BAIDU_CASSERT
#include "butil/containers/flat_map.h"           // butil::FlatMap
#include "butil/scoped_lock.h"                   // BAIDU_SCOPE_LOCK
#include "butil/string_splitter.h"               // butil::StringSplitter
#include "butil/errno.h"                         // berror
#include "butil/time.h"                          // milliseconds_from_now
#include "butil/file_util.h"                     // butil::FilePath
#include "bvar/gflag.h"
#include "bvar/variable.h"

namespace bvar {

DEFINE_bool(save_series, true,
            "Save values of last 60 seconds, last 60 minutes,"
            " last 24 hours and last 30 days for ploting");

DEFINE_bool(quote_vector, true,
            "Quote description of Vector<> to make it valid to noah");

DEFINE_bool(bvar_abort_on_same_name, false,
            "Abort when names of bvar are same");
// Remember abort request before bvar_abort_on_same_name is initialized.
static bool s_bvar_may_abort = false;
static bool validate_bvar_abort_on_same_name(const char*, bool v) {
    if (v && s_bvar_may_abort) {
        // Name conflict happens before handling args of main(), this is
        // generally caused by global bvar.
        LOG(FATAL) << "Abort due to name conflict";
        abort();
    }
    return true;
}
const bool ALLOW_UNUSED dummy_bvar_abort_on_same_name = ::GFLAGS_NS::RegisterFlagValidator(
    &FLAGS_bvar_abort_on_same_name, validate_bvar_abort_on_same_name);


DEFINE_bool(bvar_log_dumpped,  false,
            "[For debugging] print dumpped info"
            " into logstream before call Dumpper");

const size_t SUB_MAP_COUNT = 32;  // must be power of 2
BAIDU_CASSERT(!(SUB_MAP_COUNT & (SUB_MAP_COUNT - 1)), must_be_power_of_2);

class VarEntry {
public:
    VarEntry() : var(NULL), display_filter(DISPLAY_ON_ALL) {}

    Variable* var;
    DisplayFilter display_filter;
};

typedef butil::FlatMap<std::string, VarEntry> VarMap;

struct VarMapWithLock : public VarMap {
    pthread_mutex_t mutex;

    VarMapWithLock() {
        CHECK_EQ(0, init(1024, 80));
        pthread_mutexattr_t attr;
        pthread_mutexattr_init(&attr);
        pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
        pthread_mutex_init(&mutex, &attr);
        pthread_mutexattr_destroy(&attr);
    }
};

// We have to initialize global map on need because bvar is possibly used
// before main().
static pthread_once_t s_var_maps_once = PTHREAD_ONCE_INIT;
static VarMapWithLock* s_var_maps = NULL;

static void init_var_maps() {
    // It's probably slow to initialize all sub maps, but rpc often expose 
    // variables before user. So this should not be an issue to users.
    s_var_maps = new VarMapWithLock[SUB_MAP_COUNT];
}

inline size_t sub_map_index(const std::string& str) {
    if (str.empty()) {
        return 0;
    }
    size_t h = 0;
    // we're assume that str is ended with '\0', which may not be in general
    for (const char* p  = str.c_str(); *p; ++p) {
        h = h * 5 + *p;
    }
    return h & (SUB_MAP_COUNT - 1);
}

inline VarMapWithLock* get_var_maps() {
    pthread_once(&s_var_maps_once, init_var_maps);
    return s_var_maps;
}

inline VarMapWithLock& get_var_map(const std::string& name) {
    VarMapWithLock& m = get_var_maps()[sub_map_index(name)];
    return m;
}

Variable::~Variable() {
    CHECK(!hide()) << "Subclass of Variable MUST call hide() manually in their"
        " dtors to avoid displaying a variable that is just destructing";
}

int Variable::expose_impl(const butil::StringPiece& prefix,
                          const butil::StringPiece& name,
                          DisplayFilter display_filter) {
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
    
    VarMapWithLock& m = get_var_map(_name);
    {
        BAIDU_SCOPED_LOCK(m.mutex);
        VarEntry* entry = m.seek(_name);
        if (entry == NULL) {
            entry = &m[_name];
            entry->var = this;
            entry->display_filter = display_filter;
            return 0;
        }
    }
    if (FLAGS_bvar_abort_on_same_name) {
        LOG(FATAL) << "Abort due to name conflict";
        abort();
    } else if (!s_bvar_may_abort) {
        // Mark name conflict occurs, If this conflict happens before
        // initialization of bvar_abort_on_same_name, the validator will
        // abort the program if needed.
        s_bvar_may_abort = true;
    }
        
    LOG(ERROR) << "Already exposed `" << _name << "' whose value is `"
               << describe_exposed(_name) << '\'';
    _name.clear();
    return -1;
}

bool Variable::hide() {
    if (_name.empty()) {
        return false;
    }
    VarMapWithLock& m = get_var_map(_name);
    BAIDU_SCOPED_LOCK(m.mutex);
    VarEntry* entry = m.seek(_name);
    if (entry) {
        CHECK_EQ(1UL, m.erase(_name));
    } else {
        CHECK(false) << "`" << _name << "' must exist";
    }
    _name.clear();
    return true;
}

void Variable::list_exposed(std::vector<std::string>* names,
                            DisplayFilter display_filter) {
    if (names == NULL) {
        return;
    }
    names->clear();
    if (names->capacity() < 32) {
        names->reserve(count_exposed());
    }
    VarMapWithLock* var_maps = get_var_maps();
    for (size_t i = 0; i < SUB_MAP_COUNT; ++i) {
        VarMapWithLock& m = var_maps[i];
        std::unique_lock<pthread_mutex_t> mu(m.mutex);
        size_t n = 0;
        for (VarMap::const_iterator it = m.begin(); it != m.end(); ++it) {
            if (++n >= 256/*max iterated one pass*/) {
                VarMap::PositionHint hint;
                m.save_iterator(it, &hint);
                n = 0;
                mu.unlock();  // yield
                mu.lock();
                it = m.restore_iterator(hint);
                if (it == m.begin()) { // resized
                    names->clear();
                }
                if (it == m.end()) {
                    break;
                }
            }
            if (it->second.display_filter & display_filter) {
                names->push_back(it->first);
            }
        }
    }
}

size_t Variable::count_exposed() {
    size_t n = 0;
    VarMapWithLock* var_maps = get_var_maps();
    for (size_t i = 0; i < SUB_MAP_COUNT; ++i) {
        n += var_maps[i].size();
    }
    return n;
}

int Variable::describe_exposed(const std::string& name, std::ostream& os,
                               bool quote_string,
                               DisplayFilter display_filter) {
    VarMapWithLock& m = get_var_map(name);
    BAIDU_SCOPED_LOCK(m.mutex);
    VarEntry* p = m.seek(name);
    if (p == NULL) {
        return -1;
    }
    if (!(display_filter & p->display_filter)) {
        return -1;
    }
    p->var->describe(os, quote_string);
    return 0;
}

std::string Variable::describe_exposed(const std::string& name,
                                       bool quote_string,
                                       DisplayFilter display_filter) {
    std::ostringstream oss;
    if (describe_exposed(name, oss, quote_string, display_filter) == 0) {
        return oss.str();
    }
    return std::string();
}

std::string Variable::get_description() const {
    std::ostringstream os;
    describe(os, false);
    return os.str();
}

#ifdef BAIDU_INTERNAL
void Variable::get_value(boost::any* value) const {
    std::ostringstream os;
    describe(os, false);
    *value = os.str();
}
#endif

int Variable::describe_series_exposed(const std::string& name,
                                      std::ostream& os,
                                      const SeriesOptions& options) {
    VarMapWithLock& m = get_var_map(name);
    BAIDU_SCOPED_LOCK(m.mutex);
    VarEntry* p = m.seek(name);
    if (p == NULL) {
        return -1;
    }
    return p->var->describe_series(os, options);
}

#ifdef BAIDU_INTERNAL
int Variable::get_exposed(const std::string& name, boost::any* value) {
    VarMapWithLock& m = get_var_map(name);
    BAIDU_SCOPED_LOCK(m.mutex);
    VarEntry* p = m.seek(name);
    if (p == NULL) {
        return -1;
    }
    p->var->get_value(value);
    return 0;
}
#endif

// TODO(gejun): This is copied from otherwhere, common it if possible.

// Underlying buffer to store logs. Comparing to using std::ostringstream
// directly, this utility exposes more low-level methods so that we avoid
// creation of std::string which allocates memory internally.
class CharArrayStreamBuf : public std::streambuf {
public:
    explicit CharArrayStreamBuf() : _data(NULL), _size(0) {}
    ~CharArrayStreamBuf();

    int overflow(int ch) override;
    int sync() override;
    void reset();
    butil::StringPiece data() {
        return butil::StringPiece(pbase(), pptr() - pbase());
    }

private:
    char* _data;
    size_t _size;
};

CharArrayStreamBuf::~CharArrayStreamBuf() {
    free(_data);
}

int CharArrayStreamBuf::overflow(int ch) {
    if (ch == std::streambuf::traits_type::eof()) {
        return ch;
    }
    size_t new_size = std::max(_size * 3 / 2, (size_t)64);
    char* new_data = (char*)malloc(new_size);
    if (BAIDU_UNLIKELY(new_data == NULL)) {
        setp(NULL, NULL);
        return std::streambuf::traits_type::eof();
    }
    memcpy(new_data, _data, _size);
    free(_data);
    _data = new_data;
    const size_t old_size = _size;
    _size = new_size;
    setp(_data, _data + new_size);
    pbump(old_size);
    // if size == 1, this function will call overflow again.
    return sputc(ch);
}

int CharArrayStreamBuf::sync() {
    // data are already there.
    return 0;
}

void CharArrayStreamBuf::reset() {
    setp(_data, _data + _size);
}


// Written by Jack Handy
// <A href="mailto:jakkhandy@hotmail.com">jakkhandy@hotmail.com</A>
inline bool wildcmp(const char* wild, const char* str, char question_mark) {
    const char* cp = NULL;
    const char* mp = NULL;

    while (*str && *wild != '*') {
        if (*wild != *str && *wild != question_mark) {
            return false;
        }
        ++wild;
        ++str;
    }

    while (*str) {
        if (*wild == '*') {
            if (!*++wild) {
                return true;
            }
            mp = wild;
            cp = str+1;
        } else if (*wild == *str || *wild == question_mark) {
            ++wild;
            ++str;
        } else {
            wild = mp;
            str = cp++;
        }
    }

    while (*wild == '*') {
        ++wild;
    }
    return !*wild;
}

class WildcardMatcher {
public:
    WildcardMatcher(const std::string& wildcards,
                    char question_mark,
                    bool on_both_empty)
        : _question_mark(question_mark)
        , _on_both_empty(on_both_empty) {
        if (wildcards.empty()) {
            return;
        }
        std::string name;
        const char wc_pattern[3] = { '*', question_mark, '\0' };
        for (butil::StringMultiSplitter sp(wildcards.c_str(), ",;");
             sp != NULL; ++sp) {
            name.assign(sp.field(), sp.length());
            if (name.find_first_of(wc_pattern) != std::string::npos) {
                if (_wcs.empty()) {
                    _wcs.reserve(8);
                }
                _wcs.push_back(name);
            } else {
                _exact.insert(name);
            }
        }
    }
    
    bool match(const std::string& name) const {
        if (!_exact.empty()) {
            if (_exact.find(name) != _exact.end()) {
                return true;
            }
        } else if (_wcs.empty()) {
            return _on_both_empty;
        }
        for (size_t i = 0; i < _wcs.size(); ++i) {
            if (wildcmp(_wcs[i].c_str(), name.c_str(), _question_mark)) {
                return true;
            }
        }
        return false;
    }

    const std::vector<std::string>& wildcards() const { return _wcs; }
    const std::set<std::string>& exact_names() const { return _exact; }

private:
    char _question_mark;
    bool _on_both_empty;
    std::vector<std::string> _wcs;
    std::set<std::string> _exact;
};

DumpOptions::DumpOptions()
    : quote_string(true)
    , question_mark('?')
    , display_filter(DISPLAY_ON_PLAIN_TEXT)
{}

int Variable::dump_exposed(Dumper* dumper, const DumpOptions* poptions) {
    if (NULL == dumper) {
        LOG(ERROR) << "Parameter[dumper] is NULL";
        return -1;
    }
    DumpOptions opt;
    if (poptions) {
        opt = *poptions;
    }
    CharArrayStreamBuf streambuf;
    std::ostream os(&streambuf);
    int count = 0;
    WildcardMatcher black_matcher(opt.black_wildcards,
                                  opt.question_mark,
                                  false);
    WildcardMatcher white_matcher(opt.white_wildcards,
                                  opt.question_mark,
                                  true);

    std::ostringstream dumpped_info;
    const bool log_dummped = FLAGS_bvar_log_dumpped;

    if (white_matcher.wildcards().empty() &&
        !white_matcher.exact_names().empty()) {
        for (std::set<std::string>::const_iterator
                 it = white_matcher.exact_names().begin();
             it != white_matcher.exact_names().end(); ++it) {
            const std::string& name = *it;
            if (!black_matcher.match(name)) {
                if (bvar::Variable::describe_exposed(
                        name, os, opt.quote_string, opt.display_filter) != 0) {
                    continue;
                }
                if (log_dummped) {
                    dumpped_info << '\n' << name << ": " << streambuf.data();
                }
                if (!dumper->dump(name, streambuf.data())) {
                    return -1;
                }
                streambuf.reset();
                ++count;
            }
        }
    } else {
        // Have to iterate all variables.
        std::vector<std::string> varnames;
        bvar::Variable::list_exposed(&varnames, opt.display_filter);
        // Sort the names to make them more readable.
        std::sort(varnames.begin(), varnames.end());
        for (std::vector<std::string>::const_iterator
                 it = varnames.begin(); it != varnames.end(); ++it) {
            const std::string& name = *it;
            if (white_matcher.match(name) && !black_matcher.match(name)) {
                if (bvar::Variable::describe_exposed(
                        name, os, opt.quote_string, opt.display_filter) != 0) {
                    continue;
                }
                if (log_dummped) {
                    dumpped_info << '\n' << name << ": " << streambuf.data();
                }
                if (!dumper->dump(name, streambuf.data())) {
                    return -1;
                }
                streambuf.reset();
                ++count;
            }
        }
    }
    if (log_dummped) {
        LOG(INFO) << "Dumpped variables:" << dumpped_info.str();
    }
    return count;
}


// ============= export to files ==============

std::string read_command_name() {
    std::ifstream fin("/proc/self/stat");
    if (!fin.is_open()) {
        return std::string();
    }
    int pid = 0;
    std::string command_name;
    fin >> pid >> command_name;
    if (!fin.good()) {
        return std::string();
    }
    // Although the man page says the command name is in parenthesis, for
    // safety we normalize the name.
    std::string s;
    if (command_name.size() >= 2UL && command_name[0] == '(' &&
        butil::back_char(command_name) == ')') {
        // remove parenthesis.
        to_underscored_name(&s,
                            butil::StringPiece(command_name.data() + 1, 
                                              command_name.size() - 2UL));
    } else {
        to_underscored_name(&s, command_name);
    }
    return s;
}

class FileDumper : public Dumper {
public:
    FileDumper(const std::string& filename, butil::StringPiece s/*prefix*/)
        : _filename(filename), _fp(NULL) {
        // setting prefix.
        // remove trailing spaces.
        const char* p = s.data() + s.size();
        for (; p != s.data() && isspace(p[-1]); --p) {}
        s.remove_suffix(s.data() + s.size() - p);
        // normalize it.
        if (!s.empty()) {
            to_underscored_name(&_prefix, s);
            if (butil::back_char(_prefix) != '_') {
                _prefix.push_back('_');
            }
        }
    }

    ~FileDumper() {
        close();
    }
    void close() {
        if (_fp) {
            fclose(_fp);
            _fp = NULL;
        }
    }
    bool dump(const std::string& name, const butil::StringPiece& desc) override {
        if (_fp == NULL) {
            butil::File::Error error;
            butil::FilePath dir = butil::FilePath(_filename).DirName();
            if (!butil::CreateDirectoryAndGetError(dir, &error)) {
                LOG(ERROR) << "Fail to create directory=`" << dir.value()
                           << "', " << error;
                return false;
            }
            _fp = fopen(_filename.c_str(), "w");
            if (NULL == _fp) {
                LOG(ERROR) << "Fail to open " << _filename;
                return false;
            }
        }
        if (fprintf(_fp, "%.*s%.*s : %.*s\r\n",
                    (int)_prefix.size(), _prefix.data(),
                    (int)name.size(), name.data(),
                    (int)desc.size(), desc.data()) < 0) {
            PLOG(ERROR) << "Fail to write into " << _filename;
            return false;
        }
        return true;
    }
private:

    std::string _filename;
    FILE* _fp;
    std::string _prefix;
};

class FileDumperGroup : public Dumper {
public:
    FileDumperGroup(std::string tabs, std::string filename, 
                     butil::StringPiece s/*prefix*/) {
        butil::FilePath path(filename);
        if (path.FinalExtension() == ".data") {
            // .data will be appended later
            path = path.RemoveFinalExtension();
        }

        for (butil::KeyValuePairsSplitter sp(tabs, ';', '='); sp; ++sp) {
            std::string key = sp.key().as_string();
            std::string value = sp.value().as_string();
            FileDumper *f = new FileDumper(
                    path.AddExtension(key).AddExtension("data").value(), s);
            WildcardMatcher *m = new WildcardMatcher(value, '?', true);
            dumpers.push_back(std::make_pair(f, m));
        }
        dumpers.push_back(std::make_pair(
                    new FileDumper(path.AddExtension("data").value(), s), 
                    (WildcardMatcher *)NULL));
    }
    ~FileDumperGroup() {
        for (size_t i = 0; i < dumpers.size(); ++i) {
            delete dumpers[i].first;
            delete dumpers[i].second;
        }
        dumpers.clear();
    }

    bool dump(const std::string& name, const butil::StringPiece& desc) override {
        for (size_t i = 0; i < dumpers.size() - 1; ++i) {
            if (dumpers[i].second->match(name)) {
                return dumpers[i].first->dump(name, desc);
            }
        }
        // dump to default file
        return dumpers.back().first->dump(name, desc);
    }
private:
    std::vector<std::pair<FileDumper *, WildcardMatcher*> > dumpers;
};

static pthread_once_t dumping_thread_once = PTHREAD_ONCE_INIT;
static bool created_dumping_thread = false;
static pthread_mutex_t dump_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t dump_cond = PTHREAD_COND_INITIALIZER;

DEFINE_bool(bvar_dump, false,
            "Create a background thread dumping all bvar periodically, "
            "all bvar_dump_* flags are not effective when this flag is off");
DEFINE_int32(bvar_dump_interval, 10, "Seconds between consecutive dump");
DEFINE_string(bvar_dump_file, "monitor/bvar.<app>.data", "Dump bvar into this file");
DEFINE_string(bvar_dump_include, "", "Dump bvar matching these wildcards, "
              "separated by semicolon(;), empty means including all");
DEFINE_string(bvar_dump_exclude, "", "Dump bvar excluded from these wildcards, "
              "separated by semicolon(;), empty means no exclusion");
DEFINE_string(bvar_dump_prefix, "<app>", "Every dumped name starts with this prefix");
DEFINE_string(bvar_dump_tabs, "latency=*_latency*"
                              "; qps=*_qps*"
                              "; error=*_error*"
                              "; system=*process_*,*malloc_*,*kernel_*",
              "Dump bvar into different tabs according to the filters (seperated by semicolon), "
              "format: *(tab_name=wildcards;)");

#if !defined(BVAR_NOT_LINK_DEFAULT_VARIABLES)
// Expose bvar-releated gflags so that they're collected by noah.
// Maybe useful when debugging process of monitoring.
static GFlag s_gflag_bvar_dump_interval("bvar_dump_interval");
#endif

// The background thread to export all bvar periodically.
static void* dumping_thread(void*) {
    // NOTE: this variable was declared as static <= r34381, which was
    // destructed when program exits and caused coredumps.
    const std::string command_name = read_command_name();
    std::string last_filename;
    while (1) {
        // We can't access string flags directly because it's thread-unsafe.
        std::string filename;
        DumpOptions options;
        std::string prefix;
        std::string tabs;
        if (!GFLAGS_NS::GetCommandLineOption("bvar_dump_file", &filename)) {
            LOG(ERROR) << "Fail to get gflag bvar_dump_file";
            return NULL;
        }
        if (!GFLAGS_NS::GetCommandLineOption("bvar_dump_include",
                                          &options.white_wildcards)) {
            LOG(ERROR) << "Fail to get gflag bvar_dump_include";
            return NULL;
        }
        if (!GFLAGS_NS::GetCommandLineOption("bvar_dump_exclude",
                                          &options.black_wildcards)) {
            LOG(ERROR) << "Fail to get gflag bvar_dump_exclude";
            return NULL;
        }
        if (!GFLAGS_NS::GetCommandLineOption("bvar_dump_prefix", &prefix)) {
            LOG(ERROR) << "Fail to get gflag bvar_dump_prefix";
            return NULL;
        }
        if (!GFLAGS_NS::GetCommandLineOption("bvar_dump_tabs", &tabs)) {
            LOG(ERROR) << "Fail to get gflags bvar_dump_tabs";
            return NULL;
        }

        if (FLAGS_bvar_dump && !filename.empty()) {
            // Replace first <app> in filename with program name. We can't use
            // pid because a same binary should write the data to the same 
            // place, otherwise restarting of app may confuse noah with a lot 
            // of *.data. noah takes 1.5 days to figure out that some data is
            // outdated and to be removed.
            const size_t pos = filename.find("<app>");
            if (pos != std::string::npos) {
                filename.replace(pos, 5/*<app>*/, command_name);
            }
            if (last_filename != filename) {
                last_filename = filename;
                LOG(INFO) << "Write all bvar to " << filename << " every "
                          << FLAGS_bvar_dump_interval << " seconds.";
            }
            const size_t pos2 = prefix.find("<app>");
            if (pos2 != std::string::npos) {
                prefix.replace(pos2, 5/*<app>*/, command_name);
            }            
            FileDumperGroup dumper(tabs, filename, prefix);
            int nline = Variable::dump_exposed(&dumper, &options);
            if (nline < 0) {
                LOG(ERROR) << "Fail to dump vars into " << filename;
            }
        }

        // We need to separate the sleeping into a long interruptible sleep
        // and a short uninterruptible sleep. Doing this because we wake up
        // this thread in gflag validators. If this thread dumps just after
        // waking up from the condition, the gflags may not even be updated.
        const int post_sleep_ms = 50;
        int cond_sleep_ms = FLAGS_bvar_dump_interval * 1000 - post_sleep_ms;
        if (cond_sleep_ms < 0) {
            LOG(ERROR) << "Bad cond_sleep_ms=" << cond_sleep_ms;
            cond_sleep_ms = 10000;
        }
        timespec deadline = butil::milliseconds_from_now(cond_sleep_ms);
        pthread_mutex_lock(&dump_mutex);
        pthread_cond_timedwait(&dump_cond, &dump_mutex, &deadline);
        pthread_mutex_unlock(&dump_mutex);
        usleep(post_sleep_ms * 1000);
    }
}

static void launch_dumping_thread() {
    pthread_t thread_id;
    int rc = pthread_create(&thread_id, NULL, dumping_thread, NULL);
    if (rc != 0) {
        LOG(FATAL) << "Fail to launch dumping thread: " << berror(rc);
        return;
    }
    // Detach the thread because no one would join it.
    CHECK_EQ(0, pthread_detach(thread_id));
    created_dumping_thread = true;
}

// Start dumping_thread for only once.
static bool enable_dumping_thread() {
    pthread_once(&dumping_thread_once, launch_dumping_thread);
    return created_dumping_thread; 
}

static bool validate_bvar_dump(const char*, bool enabled) {
    if (enabled) {
        return enable_dumping_thread();
    }
    return true;
}
const bool ALLOW_UNUSED dummy_bvar_dump = ::GFLAGS_NS::RegisterFlagValidator(
    &FLAGS_bvar_dump, validate_bvar_dump);

// validators (to make these gflags reloadable in brpc)
static bool validate_bvar_dump_interval(const char*, int32_t v) {
    // FIXME: -bvar_dump_interval is actually unreloadable but we need to 
    // check validity of it, so we still add this validator. In practice
    // this is just fine since people rarely have the intention of modifying
    // this flag at runtime.
    if (v < 1) {
        LOG(ERROR) << "Invalid bvar_dump_interval=" << v;
        return false;
    }
    return true;
}
const bool ALLOW_UNUSED dummy_bvar_dump_interval = ::GFLAGS_NS::RegisterFlagValidator(
    &FLAGS_bvar_dump_interval, validate_bvar_dump_interval);

static bool validate_bvar_log_dumpped(const char *, bool) { return true; }
const bool ALLOW_UNUSED dummy_bvar_log_dumpped = ::GFLAGS_NS::RegisterFlagValidator(
        &FLAGS_bvar_log_dumpped, validate_bvar_log_dumpped);

static bool wakeup_dumping_thread(const char*, const std::string&) {
    // We're modifying a flag, wake up dumping_thread to generate
    // a new file soon.
    pthread_cond_signal(&dump_cond);
    return true;
}

const bool ALLOW_UNUSED dummy_bvar_dump_file = ::GFLAGS_NS::RegisterFlagValidator(
    &FLAGS_bvar_dump_file, wakeup_dumping_thread);
const bool ALLOW_UNUSED dummy_bvar_dump_filter = ::GFLAGS_NS::RegisterFlagValidator(
    &FLAGS_bvar_dump_include, wakeup_dumping_thread);
const bool ALLOW_UNUSED dummy_bvar_dump_exclude = ::GFLAGS_NS::RegisterFlagValidator(
    &FLAGS_bvar_dump_exclude, wakeup_dumping_thread);
const bool ALLOW_UNUSED dummy_bvar_dump_prefix = ::GFLAGS_NS::RegisterFlagValidator(
    &FLAGS_bvar_dump_prefix, wakeup_dumping_thread);
const bool ALLOW_UNUSED dummy_bvar_dump_tabs = ::GFLAGS_NS::RegisterFlagValidator(
    &FLAGS_bvar_dump_tabs, wakeup_dumping_thread);

void to_underscored_name(std::string* name, const butil::StringPiece& src) {
    name->reserve(name->size() + src.size() + 8/*just guess*/);
    for (const char* p = src.data(); p != src.data() + src.size(); ++p) {
        if (isalpha(*p)) {
            if (*p < 'a') { // upper cases
                if (p != src.data() && !isupper(p[-1]) &&
                    butil::back_char(*name) != '_') {
                    name->push_back('_');
                }
                name->push_back(*p - 'A' + 'a');
            } else {
                name->push_back(*p);
            }
        } else if (isdigit(*p)) {
            name->push_back(*p);
        } else if (name->empty() || butil::back_char(*name) != '_') {
            name->push_back('_');
        }
    }
}

// UT don't need default variables.
#if !defined(BVAR_NOT_LINK_DEFAULT_VARIABLES)
// Without these, default_variables.o are stripped.
// At least working in gcc 4.8
extern int do_link_default_variables;
int dummy = do_link_default_variables;
#endif

}  // namespace bvar
