// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "butil/basictypes.h"
#include "butil/logging.h"

#include <gtest/gtest.h>
#include <gflags/gflags.h>

#if !BRPC_WITH_GLOG

namespace logging {
DECLARE_bool(crash_on_fatal_log);
DECLARE_int32(v);

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
    SetLogAssertHandler(NULL);
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
            ASSERT_FALSE(GFLAGS_NS::SetCommandLineOption("v", "0").empty());
            ASSERT_FALSE(GFLAGS_NS::SetCommandLineOption("vmodule", "").empty());
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

    EXPECT_FALSE(GFLAGS_NS::SetCommandLineOption("v", "1").empty());
    
    EXPECT_FALSE(GFLAGS_NS::SetCommandLineOption("vmodule",
                                               "logging_unittest=1").empty());
    EXPECT_FALSE(GFLAGS_NS::SetCommandLineOption("vmodule",
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

    EXPECT_FALSE(GFLAGS_NS::SetCommandLineOption("vmodule",
                                              "logging_unittest=0").empty());
    for (int i = 0; i < 10; ++i) {
        VLOG_NE(i) << "vlog " << i;
    }
    EXPECT_EQ("", LOG_STREAM(VERBOSE).content_str());
    EXPECT_EQ("vlog 0", LOG_STREAM(INFO).content_str());

    EXPECT_FALSE(GFLAGS_NS::SetCommandLineOption("vmodule",
                     "logging_unittest=0,logging_unittest=1").empty());
    for (int i = 0; i < 10; ++i) {
        VLOG_NE(i) << "vlog " << i;
    }
    EXPECT_EQ("vlog 1", LOG_STREAM(VERBOSE).content_str());
    EXPECT_EQ("vlog 0", LOG_STREAM(INFO).content_str());

    EXPECT_FALSE(GFLAGS_NS::SetCommandLineOption("vmodule",
                     "logging_unittest=1,logging_unittest=0").empty());
    for (int i = 0; i < 10; ++i) {
        VLOG_NE(i) << "vlog " << i;
    }
    EXPECT_EQ("", LOG_STREAM(VERBOSE).content_str());
    EXPECT_EQ("vlog 0", LOG_STREAM(INFO).content_str());

    EXPECT_FALSE(GFLAGS_NS::SetCommandLineOption("vmodule", "").empty());
    for (int i = 0; i < 10; ++i) {
        VLOG_NE(i) << "vlog " << i;
    }
    EXPECT_EQ("vlog 1", LOG_STREAM(VERBOSE).content_str());
    EXPECT_EQ("vlog 0", LOG_STREAM(INFO).content_str());

    EXPECT_FALSE(GFLAGS_NS::SetCommandLineOption("vmodule",
                                               "logg?ng_*=2").empty());
    for (int i = 0; i < 10; ++i) {
        VLOG_NE(i) << "vlog " << i;
    }
    EXPECT_EQ("vlog 1vlog 2", LOG_STREAM(VERBOSE).content_str());
    EXPECT_EQ("vlog 0", LOG_STREAM(INFO).content_str());

    EXPECT_FALSE(GFLAGS_NS::SetCommandLineOption("vmodule",
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

    EXPECT_FALSE(GFLAGS_NS::SetCommandLineOption(
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

    EXPECT_FALSE(GFLAGS_NS::SetCommandLineOption("vmodule", "").empty());
    EXPECT_FALSE(GFLAGS_NS::SetCommandLineOption("v", "1").empty());
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

}  // namespace

}  // namespace logging
#endif
