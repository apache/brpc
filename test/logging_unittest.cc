// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/basictypes.h"
#include "butil/logging.h"
#include "gperftools_helper.h"
#include "butil/files/temp_file.h"
#include "butil/popen.h"
#include <gtest/gtest.h>
#include <gflags/gflags.h>
#include <sys/stat.h>
#include <unistd.h>

#if !BRPC_WITH_GLOG

namespace logging {
DECLARE_bool(crash_on_fatal_log);
DECLARE_int32(v);
DECLARE_bool(log_func_name);
DECLARE_bool(async_log);
DECLARE_bool(async_log_in_background_always);
DECLARE_int32(max_async_log_queue_size);
DECLARE_int32(log_rotate_size_mb);
DECLARE_int32(log_rotate_max_backups);

namespace {

// Needs to be global since log assert handlers can't maintain state.
int log_sink_call_count = 0;

#if !defined(OFFICIAL_BUILD) || defined(DCHECK_ALWAYS_ON) || !defined(NDEBUG)
void LogSink(const std::string& str) {
  ++log_sink_call_count;
}
#endif

// Class to make sure any manipulations we do to the min log level are
// contained (i.e., do not affect other unit tests).
class LogStateSaver {
 public:
  LogStateSaver() : old_min_log_level_(GetMinLogLevel()) {}

  ~LogStateSaver() {
    SetMinLogLevel(old_min_log_level_);
    SetLogAssertHandler(nullptr);
    log_sink_call_count = 0;
  }

 private:
  int old_min_log_level_;

  DISALLOW_COPY_AND_ASSIGN(LogStateSaver);
};

class LoggingTest : public testing::Test {
public:
    virtual void SetUp() {
        _old_crash_on_fatal_log = ::logging::FLAGS_crash_on_fatal_log;
        ::logging::FLAGS_crash_on_fatal_log = true;
    }
    virtual void TearDown() {
        ::logging::FLAGS_crash_on_fatal_log = _old_crash_on_fatal_log;
        if (::logging::FLAGS_v != 0) {
            // Clear -verbose to avoid affecting other tests.
            ASSERT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption("v", "0").empty());
            ASSERT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption("vmodule", "").empty());
        }
    }
private:
  bool _old_crash_on_fatal_log;
  LogStateSaver log_state_saver_;
};

TEST_F(LoggingTest, LogIsOn) {
#if defined(NDEBUG)
  const bool kDfatalIsFatal = false;
#else  // defined(NDEBUG)
  const bool kDfatalIsFatal = true;
#endif  // defined(NDEBUG)

  SetMinLogLevel(BLOG_INFO);
  EXPECT_TRUE(LOG_IS_ON(INFO));
  EXPECT_TRUE(LOG_IS_ON(WARNING));
  EXPECT_TRUE(LOG_IS_ON(ERROR));
  EXPECT_TRUE(LOG_IS_ON(FATAL));
  EXPECT_TRUE(LOG_IS_ON(DFATAL));

  SetMinLogLevel(BLOG_WARNING);
  EXPECT_FALSE(LOG_IS_ON(INFO));
  EXPECT_TRUE(LOG_IS_ON(WARNING));
  EXPECT_TRUE(LOG_IS_ON(ERROR));
  EXPECT_TRUE(LOG_IS_ON(FATAL));
  EXPECT_TRUE(LOG_IS_ON(DFATAL));

  SetMinLogLevel(BLOG_ERROR);
  EXPECT_FALSE(LOG_IS_ON(INFO));
  EXPECT_FALSE(LOG_IS_ON(WARNING));
  EXPECT_TRUE(LOG_IS_ON(ERROR));
  EXPECT_TRUE(LOG_IS_ON(FATAL));
  EXPECT_TRUE(LOG_IS_ON(DFATAL));

  // LOG_IS_ON(FATAL) should always be true.
  SetMinLogLevel(BLOG_FATAL + 1);
  EXPECT_FALSE(LOG_IS_ON(INFO));
  EXPECT_FALSE(LOG_IS_ON(WARNING));
  EXPECT_FALSE(LOG_IS_ON(ERROR));
  EXPECT_TRUE(LOG_IS_ON(FATAL));
  EXPECT_TRUE(kDfatalIsFatal == LOG_IS_ON(DFATAL));
}

TEST_F(LoggingTest, DebugLoggingReleaseBehavior) {
#if !defined(NDEBUG)
  int debug_only_variable = 1;
#endif
  // These should avoid emitting references to |debug_only_variable|
  // in release mode.
  DLOG_IF(INFO, debug_only_variable) << "test";
  DLOG_ASSERT(debug_only_variable) << "test";
  DPLOG_IF(INFO, debug_only_variable) << "test";
  DVLOG_IF(1, debug_only_variable) << "test";
}

TEST_F(LoggingTest, Dcheck) {
#if defined(NDEBUG) && !defined(DCHECK_ALWAYS_ON)
  // Release build.
  EXPECT_FALSE(DCHECK_IS_ON());
  EXPECT_FALSE(DLOG_IS_ON(DCHECK));
#elif defined(NDEBUG) && defined(DCHECK_ALWAYS_ON)
  // Release build with real DCHECKS.
  SetLogAssertHandler(&LogSink);
  EXPECT_TRUE(DCHECK_IS_ON());
  EXPECT_FALSE(DLOG_IS_ON(DCHECK));
#else
  // Debug build.
  SetLogAssertHandler(&LogSink);
  EXPECT_TRUE(DCHECK_IS_ON());
  EXPECT_TRUE(DLOG_IS_ON(DCHECK));
#endif

  EXPECT_EQ(0, log_sink_call_count);
  DCHECK(false);
  EXPECT_EQ(DCHECK_IS_ON() ? 1 : 0, log_sink_call_count);
  DPCHECK(false);
  EXPECT_EQ(DCHECK_IS_ON() ? 2 : 0, log_sink_call_count);
  DCHECK_EQ(0, 1);
  EXPECT_EQ(DCHECK_IS_ON() ? 3 : 0, log_sink_call_count);
}

TEST_F(LoggingTest, DcheckReleaseBehavior) {
  int some_variable = 1;
  // These should still reference |some_variable| so we don't get
  // unused variable warnings.
  DCHECK(some_variable) << "test";
  DPCHECK(some_variable) << "test";
  DCHECK_EQ(some_variable, 1) << "test";
}

TEST_F(LoggingTest, streaming_log_sanity) {
    ::logging::FLAGS_crash_on_fatal_log = false;

    LOG(WARNING) << 1 << 1.1f << 2l << "apple" << noflush;
    LOG(WARNING) << " orange" << noflush;
    ASSERT_EQ("11.12apple orange", LOG_STREAM(WARNING).content_str());
    ASSERT_EQ("", LOG_STREAM(WARNING).content_str());
    
    LOG(FATAL) << 1 << 1.1f << 2l << "apple" << noflush;
    LOG(FATAL) << " orange" << noflush;
    ASSERT_EQ("11.12apple orange", LOG_STREAM(FATAL).content_str());
    ASSERT_EQ("", LOG_STREAM(FATAL).content_str());

    LOG(TRACE) << 1 << 1.1f << 2l << "apple" << noflush;
    LOG(TRACE) << " orange" << noflush;
    ASSERT_EQ("11.12apple orange", LOG_STREAM(TRACE).content_str());
    ASSERT_EQ("", LOG_STREAM(TRACE).content_str());
    
    LOG(NOTICE) << 1 << 1.1f << 2l << "apple" << noflush;
    LOG(DEBUG) << 1 << 1.1f << 2l << "apple" << noflush;

    LOG(FATAL) << 1 << 1.1f << 2l << "apple";
    LOG(ERROR) << 1 << 1.1f << 2l << "apple";
    LOG(WARNING) << 1 << 1.1f << 2l << "apple";
    LOG(INFO) << 1 << 1.1f << 2l << "apple";
    LOG(TRACE) << 1 << 1.1f << 2l << "apple";
    LOG(NOTICE) << 2 << 2.2f << 3l << "orange" << noflush;
    ASSERT_EQ("11.12apple22.23orange", LOG_STREAM(NOTICE).content_str());
    LOG(DEBUG) << 1 << 1.1f << 2l << "apple";

    errno = EINVAL;
    PLOG(FATAL) << "Error occurred" << noflush;
    ASSERT_EQ("Error occurred: Invalid argument", PLOG_STREAM(FATAL).content_str());
    
    errno = 0;
    PLOG(FATAL) << "Error occurred" << noflush;
#if defined(OS_LINUX)
    ASSERT_EQ("Error occurred: Success", PLOG_STREAM(FATAL).content_str());
#else
    ASSERT_EQ("Error occurred: Undefined error: 0", PLOG_STREAM(FATAL).content_str());
#endif

    errno = EINTR;
    PLOG(FATAL) << "Error occurred" << noflush;
    ASSERT_EQ("Error occurred: Interrupted system call",
              PLOG_STREAM(FATAL).content_str());
}

TEST_F(LoggingTest, log_at) {
    ::logging::StringSink log_str;
    ::logging::LogSink* old_sink = ::logging::SetLogSink(&log_str);
    LOG_AT(WARNING, "specified_file.cc", 12345) << "file/line is specified";
    // the file:line part should be using the argument given by us.
    ASSERT_NE(std::string::npos, log_str.find("specified_file.cc:12345"));
    // restore the old sink.
    ::logging::SetLogSink(old_sink);
}

#define VLOG_NE(verbose_level) VLOG(verbose_level) << noflush

#define VLOG2_NE(virtual_path, verbose_level)           \
    VLOG2(virtual_path, verbose_level) << noflush

TEST_F(LoggingTest, vlog_sanity) {
    ::logging::FLAGS_crash_on_fatal_log = false;

    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption("v", "1").empty());
    
    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption("vmodule",
                                               "logging_unittest=1").empty());
    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption("vmodule",
                                               "logging_UNITTEST=2").empty());

    for (int i = 0; i < 10; ++i) {
        VLOG_NE(i) << "vlog " << i;
    }
    EXPECT_EQ("vlog 1vlog 2", LOG_STREAM(VERBOSE).content_str());
    EXPECT_EQ("vlog 0", LOG_STREAM(INFO).content_str());
    
    VLOG_NE(-1) << "nothing";
    EXPECT_EQ("", LOG_STREAM(VERBOSE).content_str());

    // VLOG(0) is LOG(INFO)
    VLOG_NE(0) << "always on";
    EXPECT_EQ("always on", LOG_STREAM(INFO).content_str());

    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption("vmodule",
                                              "logging_unittest=0").empty());
    for (int i = 0; i < 10; ++i) {
        VLOG_NE(i) << "vlog " << i;
    }
    EXPECT_EQ("", LOG_STREAM(VERBOSE).content_str());
    EXPECT_EQ("vlog 0", LOG_STREAM(INFO).content_str());

    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption("vmodule",
                     "logging_unittest=0,logging_unittest=1").empty());
    for (int i = 0; i < 10; ++i) {
        VLOG_NE(i) << "vlog " << i;
    }
    EXPECT_EQ("vlog 1", LOG_STREAM(VERBOSE).content_str());
    EXPECT_EQ("vlog 0", LOG_STREAM(INFO).content_str());

    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption("vmodule",
                     "logging_unittest=1,logging_unittest=0").empty());
    for (int i = 0; i < 10; ++i) {
        VLOG_NE(i) << "vlog " << i;
    }
    EXPECT_EQ("", LOG_STREAM(VERBOSE).content_str());
    EXPECT_EQ("vlog 0", LOG_STREAM(INFO).content_str());

    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption("vmodule", "").empty());
    for (int i = 0; i < 10; ++i) {
        VLOG_NE(i) << "vlog " << i;
    }
    EXPECT_EQ("vlog 1", LOG_STREAM(VERBOSE).content_str());
    EXPECT_EQ("vlog 0", LOG_STREAM(INFO).content_str());

    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption("vmodule",
                                               "logg?ng_*=2").empty());
    for (int i = 0; i < 10; ++i) {
        VLOG_NE(i) << "vlog " << i;
    }
    EXPECT_EQ("vlog 1vlog 2", LOG_STREAM(VERBOSE).content_str());
    EXPECT_EQ("vlog 0", LOG_STREAM(INFO).content_str());

    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption("vmodule",
        "foo=3,logging_unittest=3, logg?ng_*=2 , logging_*=1 ").empty());
    for (int i = 0; i < 10; ++i) {
        VLOG_NE(i) << "vlog " << i;
    }
    EXPECT_EQ("vlog 1vlog 2vlog 3", LOG_STREAM(VERBOSE).content_str());
    EXPECT_EQ("vlog 0", LOG_STREAM(INFO).content_str());

    for (int i = 0; i < 10; ++i) {
        VLOG_IF(i, i % 2 == 1) << "vlog " << i << noflush;
    }
    EXPECT_EQ("vlog 1vlog 3", LOG_STREAM(VERBOSE).content_str());

    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption(
                     "vmodule",
                     "foo/bar0/0=2,foo/bar/1=3, 2=4, foo/*/3=5, */ba?/4=6,"
                     "/5=7,/foo/bar/6=8,foo2/bar/7=9,foo/bar/8=9").empty());
    VLOG2_NE("foo/bar/0", 2) << " vlog0";
    VLOG2_NE("foo/bar0/0", 2) << " vlog0.0";
    VLOG2_NE("foo/bar/1", 3) << " vlog1";
    VLOG2_NE("foo/bar/2", 4) << " vlog2";
    VLOG2_NE("foo/bar2/2", 4) << " vlog2.2";
    VLOG2_NE("foo/bar/3", 5) << " vlog3";
    VLOG2_NE("foo/bar/4", 6) << " vlog4";
    VLOG2_NE("foo/bar/5", 7) << " vlog5";
    VLOG2_NE("foo/bar/6", 8) << " vlog6";
    VLOG2_NE("foo/bar/7", 9) << " vlog7";
    VLOG2_NE("foo/bar/8", 10) << " vlog8";
    VLOG2_NE("foo/bar/9", 11) << " vlog9";
    EXPECT_EQ(" vlog0.0 vlog1 vlog2 vlog2.2 vlog3 vlog4",
              LOG_STREAM(VERBOSE).content_str());

    // Make sure verbose log is not flushed to other levels.
    ASSERT_TRUE(LOG_STREAM(FATAL).content_str().empty());
    ASSERT_TRUE(LOG_STREAM(ERROR).content_str().empty());
    ASSERT_TRUE(LOG_STREAM(WARNING).content_str().empty());
    ASSERT_TRUE(LOG_STREAM(NOTICE).content_str().empty());
    ASSERT_TRUE(LOG_STREAM(INFO).content_str().empty());
}

TEST_F(LoggingTest, check) {
    ::logging::FLAGS_crash_on_fatal_log = false;

    CHECK(1 < 2);
    CHECK(1 > 2);
    int a = 1;
    int b = 2;
    CHECK(a > b) << "bad! a=" << a << " b=" << b;

    CHECK_EQ(a, b) << "a=" << a << " b=" << b;
    CHECK_EQ(1, 1) << "a=" << a << " b=" << b;

    CHECK_NE(2, 1);
    CHECK_NE(1, 2) << "blah0";
    CHECK_NE(2, 2) << "blah1";
    
    CHECK_LT(2, 3);
    CHECK_LT(3, 2) << "blah2";
    CHECK_LT(3, 3) << "blah3";

    CHECK_LE(2, 3);
    CHECK_LE(3, 2) << "blah4";
    CHECK_LE(3, 3);

    CHECK_GT(3, 2);
    CHECK_GT(1, 2) << "1 can't be greater than 2";
    CHECK_GT(3, 3) << "blah5";

    CHECK_GE(3, 2);
    CHECK_GE(2, 3) << "blah6";
    CHECK_GE(3, 3);
}

TEST_F(LoggingTest, log_backtrace) {
    LOG_BACKTRACE_IF(INFO, true) << "log_backtrace_if";
    LOG_BACKTRACE_IF_ONCE(INFO, true) << "log_backtrace_if_once";
    LOG_BACKTRACE_ONCE(INFO) << "log_backtrace_once";
}

int foo(int* p) {
    return ++*p;
}

TEST_F(LoggingTest, debug_level) {
    ::logging::FLAGS_crash_on_fatal_log = false;

    int run_foo = 0;
    LOG(DEBUG) << foo(&run_foo) << noflush;
    LOG(DEBUG) << foo(&run_foo);
    
    DLOG(FATAL) << foo(&run_foo);
    DLOG(WARNING) << foo(&run_foo);
    DLOG(TRACE) << foo(&run_foo);
    DLOG(NOTICE) << foo(&run_foo);
    DLOG(DEBUG) << foo(&run_foo);

    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption("vmodule", "").empty());
    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption("v", "1").empty());
    DVLOG(1) << foo(&run_foo);
    DVLOG2("a/b/c", 1) << foo(&run_foo);

#ifdef NDEBUG
    ASSERT_EQ(0, run_foo);
#else
    ASSERT_EQ(9, run_foo);
#endif    
}

static void need_ostream(std::ostream& os, const char* s) {
    os << s;
}

TEST_F(LoggingTest, as_ostream) {
    ::logging::FLAGS_crash_on_fatal_log = false;

    need_ostream(LOG_STREAM(WARNING) << noflush, "hello");
    ASSERT_EQ("hello", LOG_STREAM(WARNING).content_str());

    need_ostream(LOG_STREAM(WARNING), "hello");
    ASSERT_EQ("", LOG_STREAM(WARNING).content_str());

    need_ostream(LOG_STREAM(INFO) << noflush, "world");
    ASSERT_EQ("world", LOG_STREAM(INFO).content_str());

    need_ostream(LOG_STREAM(INFO), "world");
    ASSERT_EQ("", LOG_STREAM(INFO).content_str());

    LOG(WARNING) << 1.123456789;
    const std::streamsize saved_prec = LOG_STREAM(WARNING).precision(2);
    LOG(WARNING) << 1.123456789;
    LOG_STREAM(WARNING).precision(saved_prec);
    LOG(WARNING) << 1.123456789;
}

TEST_F(LoggingTest, limited_logging) {
    for (int i = 0; i < 100000; ++i) {
        LOG_ONCE(INFO) << "HEHE1";
        LOG_ONCE(INFO) << "HEHE2";
        VLOG_ONCE(1) << "VHEHE3";
        VLOG_ONCE(1) << "VHEHE4";
        LOG_EVERY_N(INFO, 10000) << "i1=" << i;
        LOG_EVERY_N(INFO, 5000) << "i2=" << i;
        VLOG_EVERY_N(1, 10000) << "vi3=" << i;
        VLOG_EVERY_N(1, 5000) << "vi4=" << i;
    }
    for (int i = 0; i < 300; ++i) {
        LOG_EVERY_SECOND(INFO) << "i1=" << i;
        LOG_EVERY_SECOND(INFO) << "i2=" << i;
        VLOG_EVERY_SECOND(1) << "vi3=" << i;
        VLOG_EVERY_SECOND(1) << "vi4=" << i;
        usleep(10000);
    }
}

void CheckFunctionName() {
    const char* func_name = __func__;
    DCHECK(1) << "test";
    ASSERT_EQ(func_name, LOG_STREAM(DCHECK).func());

    LOG(DEBUG) << "test" << noflush;
    ASSERT_EQ(func_name, LOG_STREAM(DEBUG).func());
    LOG(INFO) << "test" << noflush;
    ASSERT_EQ(func_name, LOG_STREAM(INFO).func());
    LOG(WARNING) << "test" << noflush;
    ASSERT_EQ(func_name, LOG_STREAM(WARNING).func());
    LOG(WARNING) << "test" << noflush;
    ASSERT_EQ(func_name, LOG_STREAM(WARNING).func());
    LOG(ERROR) << "test" << noflush;
    ASSERT_EQ(func_name, LOG_STREAM(ERROR).func());
    LOG(FATAL) << "test" << noflush;
    ASSERT_EQ(func_name, LOG_STREAM(FATAL).func());

    errno = EINTR;
    PLOG(DEBUG) << "test" << noflush;
    ASSERT_EQ(func_name, PLOG_STREAM(DEBUG).func());
    PLOG(INFO) << "test" << noflush;
    ASSERT_EQ(func_name, PLOG_STREAM(INFO).func());
    PLOG(WARNING) << "test" << noflush;
    ASSERT_EQ(func_name, PLOG_STREAM(WARNING).func());
    PLOG(WARNING) << "test" << noflush;
    ASSERT_EQ(func_name, PLOG_STREAM(WARNING).func());
    PLOG(ERROR) << "test" << noflush;
    ASSERT_EQ(func_name, PLOG_STREAM(ERROR).func());
    PLOG(FATAL) << "test" << noflush;
    ASSERT_EQ(func_name, PLOG_STREAM(FATAL).func());

    ::logging::StringSink log_str;
    ::logging::LogSink* old_sink = ::logging::SetLogSink(&log_str);
    LOG_AT(WARNING, "specified_file.cc", 12345, "log_at") << "file/line is specified";
    // the file:line part should be using the argument given by us.
    ASSERT_NE(std::string::npos, log_str.find("specified_file.cc:12345 log_at"));
    ::logging::SetLogSink(old_sink);

    EXPECT_FALSE(GFLAGS_NAMESPACE::SetCommandLineOption("v", "1").empty());
    VLOG(100) << "test" << noflush;
    ASSERT_EQ(func_name, VLOG_STREAM(100).func());
}

TEST_F(LoggingTest, log_func) {
    bool old_crash_on_fatal_log = ::logging::FLAGS_crash_on_fatal_log;
    ::logging::FLAGS_crash_on_fatal_log = false;

    ::logging::FLAGS_log_func_name = true;
    CheckFunctionName();
    ::logging::FLAGS_log_func_name = false;

    ::logging::FLAGS_crash_on_fatal_log = old_crash_on_fatal_log;
}

bool g_started = false;
bool g_stopped = false;
int g_prof_name_counter = 0;
butil::atomic<uint64_t> test_logging_count(0);

void* test_async_log(void* arg) {
    if (arg == nullptr) {
        return nullptr;
    }
    auto log = (std::string*)(arg);
    while (!g_stopped) {
        LOG(INFO) << *log;
        test_logging_count.fetch_add(1);
    }

    return nullptr;
}

TEST_F(LoggingTest, async_log) {
    bool saved_async_log = FLAGS_async_log;
    FLAGS_async_log = true;
    butil::TempFile temp_file;
    LoggingSettings settings;
    settings.logging_dest = LOG_TO_FILE;
    settings.log_file = temp_file.fname();
    settings.delete_old = DELETE_OLD_LOG_FILE;
    InitLogging(settings);

    std::string log = "135792468";
    int thread_num = 8;
    pthread_t threads[thread_num];
    for (int i = 0; i < thread_num; ++i) {
        ASSERT_EQ(0, pthread_create(&threads[i], nullptr, test_async_log, &log));
    }

    usleep(1000 * 500);

    g_stopped = true;
    for (int i = 0; i < thread_num; ++i) {
        pthread_join(threads[i], nullptr);
    }
    // Wait for async log thread to flush all logs to file.
    sleep(15);

    std::ostringstream oss;
    std::string cmd = butil::string_printf("grep -c %s %s",
        log.c_str(), temp_file.fname());
    ASSERT_LE(0, butil::read_command_output(oss, cmd.c_str()));
    uint64_t log_count = std::strtol(oss.str().c_str(), nullptr, 10);
    ASSERT_EQ(log_count, test_logging_count.load());

    FLAGS_async_log = saved_async_log;
}

static bool FileSize(const std::string& path, off_t* size) {
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        return false;
    }
    if (size) {
        *size = st.st_size;
    }
    return true;
}

class LogRotationTest : public LoggingTest {
public:
    void SetUp() override {
        LoggingTest::SetUp();
        _saved_async_log = FLAGS_async_log;
        _saved_rotate_size_mb = FLAGS_log_rotate_size_mb;
        _saved_max_backups = FLAGS_log_rotate_max_backups;
        FLAGS_async_log = false;
        _log_path = _temp_file.fname();
    }

    void TearDown() override {
        CloseLogFile();
        for (int i = 1; i <= _saved_max_backups + 2; ++i) {
            unlink(BackupPath(i).c_str());
        }
        FLAGS_log_rotate_max_backups = _saved_max_backups;
        FLAGS_log_rotate_size_mb = _saved_rotate_size_mb;
        FLAGS_async_log = _saved_async_log;
        LoggingTest::TearDown();
    }

protected:
    // Point the logging framework at `_log_path' and start from an empty file.
    void InitLoggingToTempFile() {
        LoggingSettings settings;
        settings.logging_dest = LOG_TO_FILE;
        settings.log_file = _log_path.c_str();
        settings.delete_old = DELETE_OLD_LOG_FILE;
        ASSERT_TRUE(InitLogging(settings));
    }

    std::string BackupPath(int index) const {
        return _log_path + butil::string_printf(".%d", index);
    }

    // Each log adds a header on top of `size', which only makes the file grow
    // faster than the caller expects, never slower.
    void WriteLogs(int count, size_t size) {
        const std::string content(size, 'x');
        for (int i = 0; i < count; ++i) {
            LOG(INFO) << content;
        }
    }

    std::string _log_path;

private:
    butil::TempFile _temp_file;
    bool _saved_async_log{false};
    int _saved_rotate_size_mb{0};
    int _saved_max_backups{0};
};

TEST_F(LogRotationTest, disabled_by_default) {
    FLAGS_log_rotate_size_mb = 0;
    InitLoggingToTempFile();

    WriteLogs(2000, 1000);  // ~2MB
    CloseLogFile();

    off_t size = 0;
    ASSERT_TRUE(FileSize(_log_path, &size));
    ASSERT_GT(size, 1L * 1024 * 1024);
    ASSERT_FALSE(FileSize(BackupPath(1), nullptr));
}

TEST_F(LogRotationTest, rotate_and_prune) {
    FLAGS_log_rotate_size_mb = 1;
    FLAGS_log_rotate_max_backups = 2;
    InitLoggingToTempFile();

    WriteLogs(3000, 1000);  // ~3MB, rotates 3 times
    CloseLogFile();

    const off_t rotate_size = 1L * 1024 * 1024;
    off_t size = 0;
    // The current file was restarted by the last rotation.
    ASSERT_TRUE(FileSize(_log_path, &size));
    ASSERT_LE(size, rotate_size);
    // ".1" is the most recent backup, older ones are pruned.
    ASSERT_TRUE(FileSize(BackupPath(1), &size));
    ASSERT_GT(size, 0);
    ASSERT_LE(size, rotate_size);
    ASSERT_TRUE(FileSize(BackupPath(2), &size));
    ASSERT_GT(size, 0);
    ASSERT_LE(size, rotate_size);
    ASSERT_FALSE(FileSize(BackupPath(3), nullptr));
}

TEST_F(LogRotationTest, keep_one_backup) {
    FLAGS_log_rotate_size_mb = 1;
    FLAGS_log_rotate_max_backups = 1;
    InitLoggingToTempFile();

    WriteLogs(3000, 1000);
    CloseLogFile();

    ASSERT_TRUE(FileSize(BackupPath(1), nullptr));
    ASSERT_FALSE(FileSize(BackupPath(2), nullptr));
}

// A log bigger than the threshold is written as a whole rather than being
// split or triggering a rotation on every single write. Only the number of
// files holding a whole oversized log is checked, so that logs written by the
// rest of the process cannot make the test flaky.
TEST_F(LogRotationTest, log_larger_than_rotate_size) {
    FLAGS_log_rotate_size_mb = 1;
    // High enough that none of the files below can be pruned.
    FLAGS_log_rotate_max_backups = 10;
    InitLoggingToTempFile();

    const size_t log_size = 2 * 1024 * 1024;
    WriteLogs(3, log_size);
    CloseLogFile();

    int whole_logs = 0;
    off_t size = 0;
    if (FileSize(_log_path, &size) && size >= (off_t)log_size) {
        ++whole_logs;
    }
    for (int i = 1; i <= FLAGS_log_rotate_max_backups; ++i) {
        if (!FileSize(BackupPath(i), &size)) {
            continue;
        }
        // A rotation never leaves an empty file behind.
        ASSERT_GT(size, 0) << BackupPath(i);
        if (size >= (off_t)log_size) {
            ++whole_logs;
        }
    }
    ASSERT_EQ(3, whole_logs);
}

// The log file is opened with "a", so the size a restarted process starts
// from must come from the file itself instead of being counted from zero.
TEST_F(LogRotationTest, size_of_existing_file_is_taken_into_account) {
    // Grow the file beyond the future threshold while rotation is still off.
    FLAGS_log_rotate_size_mb = 0;
    FLAGS_log_rotate_max_backups = 2;
    InitLoggingToTempFile();
    WriteLogs(1500, 1000);
    CloseLogFile();  // as if the process exited here

    off_t before = 0;
    ASSERT_TRUE(FileSize(_log_path, &before));
    ASSERT_GT(before, 1L * 1024 * 1024);
    ASSERT_FALSE(FileSize(BackupPath(1), nullptr));

    // Turn rotation on and write a single log. The file is reopened in append
    // mode, so this can only rotate if the size was read from the file rather
    // than counted from zero.
    FLAGS_log_rotate_size_mb = 1;
    WriteLogs(1, 10);
    CloseLogFile();

    off_t rotated = 0;
    ASSERT_TRUE(FileSize(BackupPath(1), &rotated));
    ASSERT_GE(rotated, before);
    off_t current = 0;
    ASSERT_TRUE(FileSize(_log_path, &current));
    ASSERT_LT(current, before);
}

TEST_F(LogRotationTest, async_log) {
    FLAGS_async_log = true;
    FLAGS_log_rotate_size_mb = 1;
    FLAGS_log_rotate_max_backups = 2;
    InitLoggingToTempFile();

    WriteLogs(3000, 1000);
    // The background thread writes the logs, wait for the rotations to show up
    // instead of sleeping for a fixed amount of time.
    for (int i = 0; i < 600 && !FileSize(BackupPath(2), nullptr); ++i) {
        usleep(100 * 1000);
    }
    // Drain the rest of the queue so that no leftover log is written into the
    // file of whichever test runs next.
    off_t last_size = -1;
    off_t current_size = 0;
    for (int i = 0; i < 100 && last_size != current_size; ++i) {
        last_size = current_size;
        usleep(100 * 1000);
        FileSize(_log_path, &current_size);
    }
    FLAGS_async_log = false;
    CloseLogFile();

    off_t size = 0;
    ASSERT_TRUE(FileSize(BackupPath(1), &size));
    ASSERT_GT(size, 0);
    ASSERT_LE(size, 1L * 1024 * 1024);
    ASSERT_TRUE(FileSize(BackupPath(2), nullptr));
    ASSERT_FALSE(FileSize(BackupPath(3), nullptr));
}

// Lowering --log_rotate_max_backups at run time must also remove the files
// left over from the previous, larger value.
TEST_F(LogRotationTest, lowering_max_backups_removes_leftovers) {
    FLAGS_log_rotate_size_mb = 1;
    FLAGS_log_rotate_max_backups = 5;
    InitLoggingToTempFile();

    WriteLogs(6000, 1000);  // fills every backup slot
    ASSERT_TRUE(FileSize(BackupPath(5), nullptr));

    FLAGS_log_rotate_max_backups = 2;
    WriteLogs(2000, 1000);  // rotates at least once with the new value
    CloseLogFile();

    ASSERT_TRUE(FileSize(BackupPath(1), nullptr));
    ASSERT_TRUE(FileSize(BackupPath(2), nullptr));
    ASSERT_FALSE(FileSize(BackupPath(3), nullptr));
    ASSERT_FALSE(FileSize(BackupPath(4), nullptr));
    ASSERT_FALSE(FileSize(BackupPath(5), nullptr));
}

// An external tool taking the log file away is not a rotation failure. The
// next write starts a fresh file and the existing backups must be left alone
// rather than shifted down for nothing.
TEST_F(LogRotationTest, external_rename_does_not_consume_a_backup) {
    FLAGS_log_rotate_size_mb = 1;
    FLAGS_log_rotate_max_backups = 5;
    InitLoggingToTempFile();

    WriteLogs(2500, 1000);  // rotates a couple of times
    off_t first_backup = 0;
    ASSERT_TRUE(FileSize(BackupPath(1), &first_backup));
    int backups_before = 0;
    for (int i = 1; i <= FLAGS_log_rotate_max_backups; ++i) {
        if (FileSize(BackupPath(i), nullptr)) {
            ++backups_before;
        }
    }

    // Take the file away the way an external log rotator would, then log
    // enough that a rotation would otherwise have been due.
    const std::string moved = _log_path + ".moved";
    ASSERT_EQ(0, rename(_log_path.c_str(), moved.c_str()));
    WriteLogs(800, 1000);
    CloseLogFile();

    off_t size = 0;
    ASSERT_TRUE(FileSize(BackupPath(1), &size));
    ASSERT_EQ(first_backup, size);  // ".1" was not shifted away
    int backups_after = 0;
    for (int i = 1; i <= FLAGS_log_rotate_max_backups; ++i) {
        if (FileSize(BackupPath(i), nullptr)) {
            ++backups_after;
        }
    }
    ASSERT_EQ(backups_before, backups_after);
    // Logging went on under the original name.
    ASSERT_TRUE(FileSize(_log_path, nullptr));
    unlink(moved.c_str());
}

// When the rename can not succeed, rotation must back off instead of closing
// and reopening the file on every single log.
TEST_F(LogRotationTest, backs_off_when_rotation_keeps_failing) {
    FLAGS_log_rotate_size_mb = 1;
    FLAGS_log_rotate_max_backups = 2;
    InitLoggingToTempFile();

    // Occupy every backup slot with a non-empty directory, which makes both
    // the shift and the final rename fail with EISDIR/ENOTEMPTY.
    for (int i = 1; i <= FLAGS_log_rotate_max_backups; ++i) {
        ASSERT_EQ(0, mkdir(BackupPath(i).c_str(), 0755));
        ASSERT_EQ(0, mkdir((BackupPath(i) + "/x").c_str(), 0755));
    }

    WriteLogs(1500, 1000);  // ~1.5MB, crosses the threshold
    CloseLogFile();

    // Nothing was dropped, the logs kept being appended to the current file.
    off_t size = 0;
    ASSERT_TRUE(FileSize(_log_path, &size));
    ASSERT_GT(size, 1L * 1024 * 1024);

    for (int i = 1; i <= FLAGS_log_rotate_max_backups; ++i) {
        ASSERT_EQ(0, rmdir((BackupPath(i) + "/x").c_str()));
        ASSERT_EQ(0, rmdir(BackupPath(i).c_str()));
    }
    // InitLogging() clears the back off for the tests that follow.
    InitLoggingToTempFile();
}

// An external tool renaming the log file away must not disable rotation for
// good: the file it is reopened on starts empty, which is the signal that
// rotation can be attempted again.
TEST_F(LogRotationTest, recovers_after_the_log_file_is_replaced) {
    FLAGS_log_rotate_size_mb = 1;
    FLAGS_log_rotate_max_backups = 2;
    InitLoggingToTempFile();

    for (int i = 1; i <= FLAGS_log_rotate_max_backups; ++i) {
        ASSERT_EQ(0, mkdir(BackupPath(i).c_str(), 0755));
        ASSERT_EQ(0, mkdir((BackupPath(i) + "/x").c_str(), 0755));
    }
    WriteLogs(1500, 1000);  // rotation fails and backs off
    off_t size = 0;
    ASSERT_TRUE(FileSize(_log_path, &size));
    ASSERT_GT(size, 1L * 1024 * 1024);

    for (int i = 1; i <= FLAGS_log_rotate_max_backups; ++i) {
        ASSERT_EQ(0, rmdir((BackupPath(i) + "/x").c_str()));
        ASSERT_EQ(0, rmdir(BackupPath(i).c_str()));
    }
    // Take the file away the way an external log rotator would.
    CloseLogFile();
    const std::string moved = _log_path + ".moved";
    ASSERT_EQ(0, rename(_log_path.c_str(), moved.c_str()));

    WriteLogs(1500, 1000);
    CloseLogFile();

    // Rotation works again without InitLogging() having been called.
    ASSERT_TRUE(FileSize(BackupPath(1), &size));
    ASSERT_GT(size, 0);
    ASSERT_LE(size, 1L * 1024 * 1024);
    unlink(moved.c_str());
}

#if defined(BRPC_ENABLE_CPU_PROFILER) || defined(BAIDU_RPC_ENABLE_CPU_PROFILER)
struct BAIDU_CACHELINE_ALIGNMENT PerfArgs {
    const std::string* log;
    int64_t counter;
    int64_t elapse_ns;
    bool ready;

    PerfArgs() : log(nullptr), counter(0), elapse_ns(0), ready(false) {}
};

void* test_log(void* void_arg) {
    auto args = (PerfArgs*)void_arg;
    args->ready = true;
    butil::Timer t;
    std::string log = *args->log;
    int counter = 0;
    while (!g_stopped) {
        if (g_started) {
            break;
        }
        usleep(10);
    }
    t.start();
    while (!g_stopped) {
        {
            LOG(INFO) << log;
            test_logging_count.fetch_add(1, butil::memory_order_relaxed);
        }
        ++counter;
    }
    t.stop();
    args->elapse_ns = t.n_elapsed();
    args->counter = counter;
    return nullptr;
}

void PerfTest(int thread_num, const std::string& log, bool async) {
    FLAGS_async_log = async;

    g_started = false;
    g_stopped = false;
    pthread_t threads[thread_num];
    std::vector<PerfArgs> args(thread_num);
    for (int i = 0; i < thread_num; ++i) {
        args[i].log = &log;
        ASSERT_EQ(0, pthread_create(&threads[i], nullptr, test_log, &args[i]));
    }
    while (true) {
        bool all_ready = true;
        for (int i = 0; i < thread_num; ++i) {
            if (!args[i].ready) {
                all_ready = false;
                break;
            }
        }
        if (all_ready) {
            break;
        }
        usleep(1000);
    }
    int sleep_s = 2;
    g_started = true;
    char prof_name[32];
    snprintf(prof_name, sizeof(prof_name), "logging_%d.prof", ++g_prof_name_counter);
    ProfilerStart(prof_name);
    sleep(sleep_s);
    ProfilerStop();
    g_stopped = true;
    int64_t wait_time = 0;
    int64_t count = 0;
    for (int i = 0; i < thread_num; ++i) {
        pthread_join(threads[i], nullptr);
        wait_time += args[i].elapse_ns;
        count += args[i].counter;
    }
    std::cout << " thread_num=" << thread_num
              << " log_type=" << (async ? "async" : "sync")
              << " log_size=" << log.size()
              << " count=" << count
              << " duration=" << sleep_s << "s"
              << " qps=" << (int)(count / (double)sleep_s)
              << " average_time=" << wait_time / (double)count << "us"
              << std::endl;
}

TEST_F(LoggingTest, performance) {
    bool saved_async_log = FLAGS_async_log;
    FLAGS_max_async_log_queue_size =
        std::numeric_limits<int32_t>::max();
    FLAGS_async_log_in_background_always = true;

    LoggingSettings settings;
    settings.logging_dest = LOG_TO_FILE;
    settings.delete_old = DELETE_OLD_LOG_FILE;
    InitLogging(settings);
    std::string log(100, 'a');
    PerfTest(1, log, false);
    PerfTest(8, log, false);
    PerfTest(1, log, true);
    sleep(10);
    PerfTest(8, log, true);

    FLAGS_async_log = saved_async_log;
}
#endif // BRPC_ENABLE_CPU_PROFILER || BAIDU_RPC_ENABLE_CPU_PROFILER

}  // namespace

}  // namespace logging
#endif
