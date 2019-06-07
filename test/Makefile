NEED_GPERFTOOLS=1
NEED_GTEST=1
include ../config.mk
CPPFLAGS+=-DBTHREAD_USE_FAST_PTHREAD_MUTEX -D__const__= -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -DUNIT_TEST -Dprivate=public -Dprotected=public -DBVAR_NOT_LINK_DEFAULT_VARIABLES --include sstream_workaround.h
CXXFLAGS=$(CPPFLAGS) -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-invalid-offsetof -Wno-unused-parameter -fno-omit-frame-pointer -std=c++0x

#required by butil/crc32.cc to boost performance for 10x
ifeq ($(shell test $(GCC_VERSION) -ge 40400; echo $$?),0)
	CXXFLAGS+=-msse4 -msse4.2
endif
#not solved yet
ifeq ($(CC),gcc)
 ifeq ($(shell test $(GCC_VERSION) -ge 70000; echo $$?),0)
	CXXFLAGS+=-Wno-aligned-new
 endif
endif

LIBS += .
HDRPATHS=-I. -I../src $(addprefix -I, $(HDRS))
LIBPATHS=$(addprefix -L, $(LIBS))

COMMA = ,
SOPATHS = $(addprefix -Wl$(COMMA)-rpath$(COMMA), $(LIBS))

SOEXT = so
ifeq ($(SYSTEM),Darwin)
    SOEXT = dylib
endif

TEST_BUTIL_SOURCES = \
    at_exit_unittest.cc \
    atomicops_unittest.cc \
    base64_unittest.cc \
    big_endian_unittest.cc \
    bits_unittest.cc \
    hash_tables_unittest.cc \
    linked_list_unittest.cc \
    mru_cache_unittest.cc \
    small_map_unittest.cc \
    stack_container_unittest.cc \
    cpu_unittest.cc \
    crash_logging_unittest.cc \
    leak_tracker_unittest.cc \
    stack_trace_unittest.cc \
    environment_unittest.cc \
    file_util_unittest.cc \
    dir_reader_posix_unittest.cc \
    file_path_unittest.cc \
    file_unittest.cc \
    scoped_temp_dir_unittest.cc \
    guid_unittest.cc \
    hash_unittest.cc \
    lazy_instance_unittest.cc \
    md5_unittest.cc \
    aligned_memory_unittest.cc \
    linked_ptr_unittest.cc \
    ref_counted_memory_unittest.cc \
    ref_counted_unittest.cc \
    scoped_ptr_unittest.cc \
    scoped_vector_unittest.cc \
    singleton_unittest.cc \
    weak_ptr_unittest.cc \
    observer_list_unittest.cc \
    file_descriptor_shuffle_unittest.cc \
    rand_util_unittest.cc \
    safe_numerics_unittest.cc \
    scoped_clear_errno_unittest.cc \
    scoped_generic_unittest.cc \
    security_unittest.cc \
    sha1_unittest.cc \
    stl_util_unittest.cc \
    nullable_string16_unittest.cc \
    safe_sprintf_unittest.cc \
    string16_unittest.cc \
    stringprintf_unittest.cc \
    string_number_conversions_unittest.cc \
    string_piece_unittest.cc \
    string_split_unittest.cc \
    string_tokenizer_unittest.cc \
    string_util_unittest.cc \
    stringize_macros_unittest.cc \
    sys_string_conversions_unittest.cc \
    utf_offset_string_conversions_unittest.cc \
    utf_string_conversions_unittest.cc \
    cancellation_flag_unittest.cc \
    condition_variable_unittest.cc \
    lock_unittest.cc \
    waitable_event_unittest.cc \
    type_traits_unittest.cc \
    non_thread_safe_unittest.cc \
    platform_thread_unittest.cc \
    simple_thread_unittest.cc \
    thread_checker_unittest.cc \
    thread_collision_warner_unittest.cc \
    thread_id_name_manager_unittest.cc \
    thread_local_storage_unittest.cc \
    thread_local_unittest.cc \
    watchdog_unittest.cc \
    pr_time_unittest.cc \
    time_unittest.cc \
    version_unittest.cc \
    logging_unittest.cc \
    cacheline_unittest.cpp \
    class_name_unittest.cpp \
    endpoint_unittest.cpp \
    unique_ptr_unittest.cpp \
    errno_unittest.cpp \
    fd_guard_unittest.cpp \
    file_watcher_unittest.cpp \
    find_cstr_unittest.cpp \
    scoped_lock_unittest.cpp \
    status_unittest.cpp \
    string_printf_unittest.cpp \
    string_splitter_unittest.cpp \
    synchronous_event_unittest.cpp \
    temp_file_unittest.cpp \
    baidu_thread_local_unittest.cpp \
    baidu_time_unittest.cpp \
    flat_map_unittest.cpp \
    crc32c_unittest.cc \
    iobuf_unittest.cpp \
    recordio_unittest.cpp \
    test_switches.cc \
    scoped_locale.cc \
    popen_unittest.cpp \
    butil_unittest_main.cpp

ifeq ($(SYSTEM), Linux)
    TEST_BUTIL_SOURCES += test_file_util_linux.cc \
						  proc_maps_linux_unittest.cc
endif

# bthread_* functions are used in logging.cc, and they need to be marked as
# weak symbols explicitly in Darwin system.
ifeq ($(SYSTEM), Darwin)
	DYNAMIC_LINKINGS+=-Wl,-U,_bthread_getspecific
	DYNAMIC_LINKINGS+=-Wl,-U,_bthread_setspecific
	DYNAMIC_LINKINGS+=-Wl,-U,_bthread_key_create
endif

UT_DYNAMIC_LINKINGS = $(DYNAMIC_LINKINGS) -lbrpc.dbg

TEST_BUTIL_OBJS = iobuf.pb.o $(addsuffix .o, $(basename $(TEST_BUTIL_SOURCES))) 

TEST_BVAR_SOURCES = $(wildcard bvar_*_unittest.cpp)
TEST_BVAR_OBJS = $(addsuffix .o, $(basename $(TEST_BVAR_SOURCES))) 

TEST_BTHREAD_SOURCES = $(wildcard bthread_*unittest.cpp)
TEST_BTHREAD_OBJS = $(addsuffix .o, $(basename $(TEST_BTHREAD_SOURCES))) 

TEST_BRPC_SOURCES = $(wildcard brpc_*unittest.cpp)
TEST_BRPC_OBJS = $(addsuffix .o, $(basename $(TEST_BRPC_SOURCES))) 

TEST_PROTO_SOURCES = $(wildcard *.proto)
TEST_PROTO_OBJS = $(TEST_PROTO_SOURCES:.proto=.pb.o)

TEST_BINS = test_butil test_bvar $(TEST_BTHREAD_SOURCES:.cpp=) $(TEST_BRPC_SOURCES:.cpp=)

.PHONY:all
all: $(TEST_BINS)

.PHONY:clean
clean:clean_bins
	@echo "Cleaning"
	@rm -rf $(TEST_BUTIL_OBJS) $(TEST_BVAR_OBJS) $(TEST_BTHREAD_OBJS) $(TEST_BRPC_OBJS) $(TEST_PROTO_OBJS)
	@$(MAKE) -C.. clean_debug

.PHONY:clean_bins
clean_bins:
	@rm -rf $(TEST_BINS)

libbrpc.dbg.$(SOEXT):FORCE
	@$(MAKE) -C.. test/libbrpc.dbg.$(SOEXT)

libbvar.dbg.a:FORCE
	@$(MAKE) -C.. test/libbvar.dbg.a

FORCE:

.PRECIOUS: %.o

test_butil:$(TEST_BUTIL_OBJS) | libbrpc.dbg.$(SOEXT)
	@echo "Linking $@"
ifeq ($(SYSTEM),Linux)
	@$(CXX) -o $@ $(LIBPATHS) $(SOPATHS) -Xlinker "-(" $^ -Xlinker "-)" $(STATIC_LINKINGS) $(UT_DYNAMIC_LINKINGS)
else ifeq ($(SYSTEM),Darwin)
	@$(CXX) -o $@ $(LIBPATHS) $(SOPATHS) $^ $(STATIC_LINKINGS) $(UT_DYNAMIC_LINKINGS)
endif

test_bvar:libbvar.dbg.a $(TEST_BVAR_OBJS)
	@echo "Linking $@"
ifeq ($(SYSTEM),Linux)
	@$(CXX) -o $@ $(LIBPATHS) $(SOPATHS) -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic -Xlinker "-)" $(DYNAMIC_LINKINGS)
else ifeq ($(SYSTEM),Darwin)
	@$(CXX) -o $@ $(LIBPATHS) $(SOPATHS) $^ $(STATIC_LINKINGS) $(DYNAMIC_LINKINGS)
endif

bthread%unittest:bthread%unittest.o | libbrpc.dbg.$(SOEXT)
	@echo "Linking $@"
ifeq ($(SYSTEM),Linux)
	@$(CXX) -o $@ $(LIBPATHS) $(SOPATHS) -Xlinker "-(" $^ -Xlinker "-)" $(STATIC_LINKINGS) $(UT_DYNAMIC_LINKINGS)
else ifeq ($(SYSTEM),Darwin)
	@$(CXX) -o $@ $(LIBPATHS) $(SOPATHS) $^ $(STATIC_LINKINGS) $(UT_DYNAMIC_LINKINGS)
endif

brpc_%_unittest:$(TEST_PROTO_OBJS) brpc_%_unittest.o | libbrpc.dbg.$(SOEXT)
	@echo "Linking $@"
ifeq ($(SYSTEM),Linux)
	@$(CXX) -o $@ $(LIBPATHS) $(SOPATHS) -Xlinker "-(" $^ -Xlinker "-)" $(STATIC_LINKINGS) $(UT_DYNAMIC_LINKINGS)
else ifeq ($(SYSTEM),Darwin)
	@$(CXX) -o $@ $(LIBPATHS) $(SOPATHS) $^ $(STATIC_LINKINGS) $(UT_DYNAMIC_LINKINGS)
endif

%.pb.cc %.pb.h:%.proto
	@echo "Generating $@"
	@$(PROTOC) --cpp_out=. --proto_path=. --proto_path=../src --proto_path=$(PROTOBUF_HDR) $<

baidu_time_unittest.o:baidu_time_unittest.cpp | libbrpc.dbg.$(SOEXT)
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) -O2 $(CXXFLAGS) $< -o $@

brpc_h2_unsent_message_unittest.o:brpc_h2_unsent_message_unittest.cpp | libbrpc.dbg.$(SOEXT)
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) -O2 $(CXXFLAGS) $< -o $@

%.o:%.cpp | libbrpc.dbg.$(SOEXT)
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

%.o:%.cc | libbrpc.dbg.$(SOEXT)
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

