include config.mk
CPPFLAGS=-DBTHREAD_USE_FAST_PTHREAD_MUTEX -D__const__= -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -DBRPC_REVISION=\"$(shell git rev-parse --short HEAD)\"
#Add -fno-omit-frame-pointer: perf/tcmalloc-profiler uses frame pointers by default
CXXFLAGS=$(CPPFLAGS) -O2 -g -pipe -Wall -W -Werror -fPIC -fstrict-aliasing -Wno-invalid-offsetof -Wno-unused-parameter -fno-omit-frame-pointer -std=c++0x -include brpc/config.h
CFLAGS=$(CPPFLAGS) -O2 -g -pipe -Wall -W -Werror -fPIC -fstrict-aliasing -Wno-unused-parameter -fno-omit-frame-pointer
HDRPATHS=-I. $(addprefix -I, $(HDRS))
LIBPATHS = $(addprefix -L, $(LIBS))
SRCEXTS = .c .cc .cpp .proto
HDREXTS = .h .hpp
#dyanmic linking of libprotoc.so crashes on ubuntu when protoc-gen-mcpack is invoked
STATIC_LINKINGS += -lprotoc

BASE_SOURCES = \
    base/third_party/dmg_fp/g_fmt.cc \
    base/third_party/dmg_fp/dtoa_wrapper.cc \
    base/third_party/dmg_fp/dtoa.cc \
    base/third_party/dynamic_annotations/dynamic_annotations.c \
    base/third_party/icu/icu_utf.cc \
    base/third_party/superfasthash/superfasthash.c \
    base/third_party/modp_b64/modp_b64.cc \
    base/third_party/nspr/prtime.cc \
    base/third_party/symbolize/demangle.cc \
    base/third_party/symbolize/symbolize.cc \
    base/third_party/xdg_mime/xdgmime.c \
    base/third_party/xdg_mime/xdgmimealias.c \
    base/third_party/xdg_mime/xdgmimecache.c \
    base/third_party/xdg_mime/xdgmimeglob.c \
    base/third_party/xdg_mime/xdgmimeicon.c \
    base/third_party/xdg_mime/xdgmimeint.c \
    base/third_party/xdg_mime/xdgmimemagic.c \
    base/third_party/xdg_mime/xdgmimeparent.c \
    base/third_party/xdg_user_dirs/xdg_user_dir_lookup.cc \
    base/third_party/snappy/snappy-sinksource.cc \
    base/third_party/snappy/snappy-stubs-internal.cc \
    base/third_party/snappy/snappy.cc \
    base/third_party/murmurhash3/murmurhash3.cpp \
    base/allocator/type_profiler_control.cc \
    base/arena.cpp \
    base/at_exit.cc \
    base/atomicops_internals_x86_gcc.cc \
    base/barrier_closure.cc \
    base/base_paths.cc \
    base/base_paths_posix.cc \
    base/base64.cc \
    base/base_switches.cc \
    base/big_endian.cc \
    base/bind_helpers.cc \
    base/callback_helpers.cc \
    base/callback_internal.cc \
    base/command_line.cc \
    base/cpu.cc \
    base/debug/alias.cc \
    base/debug/asan_invalid_access.cc \
    base/debug/crash_logging.cc \
    base/debug/debugger.cc \
    base/debug/debugger_posix.cc \
    base/debug/dump_without_crashing.cc \
    base/debug/proc_maps_linux.cc \
    base/debug/stack_trace.cc \
    base/debug/stack_trace_posix.cc \
    base/environment.cc \
    base/files/file.cc \
    base/files/file_posix.cc \
    base/files/file_enumerator.cc \
    base/files/file_enumerator_posix.cc \
    base/files/file_path.cc \
    base/files/file_path_constants.cc \
    base/files/memory_mapped_file.cc \
    base/files/memory_mapped_file_posix.cc \
    base/files/scoped_file.cc \
    base/files/scoped_temp_dir.cc \
    base/file_util.cc \
    base/file_util_linux.cc \
    base/file_util_posix.cc \
    base/guid.cc \
    base/guid_posix.cc \
    base/hash.cc \
    base/lazy_instance.cc \
    base/location.cc \
    base/md5.cc \
    base/memory/aligned_memory.cc \
    base/memory/ref_counted.cc \
    base/memory/ref_counted_memory.cc \
    base/memory/shared_memory_posix.cc \
    base/memory/singleton.cc \
    base/memory/weak_ptr.cc \
    base/nix/mime_util_xdg.cc \
    base/nix/xdg_util.cc \
    base/path_service.cc \
    base/posix/file_descriptor_shuffle.cc \
    base/posix/global_descriptors.cc \
    base/process/internal_linux.cc \
    base/process/kill.cc \
    base/process/kill_posix.cc \
    base/process/launch.cc \
    base/process/launch_posix.cc \
    base/process/process_handle_linux.cc \
    base/process/process_handle_posix.cc \
    base/process/process_info_linux.cc \
    base/process/process_iterator.cc \
    base/process/process_iterator_linux.cc \
    base/process/process_linux.cc \
    base/process/process_metrics.cc \
    base/process/process_metrics_linux.cc \
    base/process/process_metrics_posix.cc \
    base/process/process_posix.cc \
    base/rand_util.cc \
    base/rand_util_posix.cc \
    base/fast_rand.cpp \
    base/safe_strerror_posix.cc \
    base/sha1_portable.cc \
    base/strings/latin1_string_conversions.cc \
    base/strings/nullable_string16.cc \
    base/strings/safe_sprintf.cc \
    base/strings/string16.cc \
    base/strings/string_number_conversions.cc \
    base/strings/string_split.cc \
    base/strings/string_piece.cc \
    base/strings/string_util.cc \
    base/strings/string_util_constants.cc \
    base/strings/stringprintf.cc \
    base/strings/sys_string_conversions_posix.cc \
    base/strings/utf_offset_string_conversions.cc \
    base/strings/utf_string_conversion_utils.cc \
    base/strings/utf_string_conversions.cc \
    base/synchronization/cancellation_flag.cc \
    base/synchronization/condition_variable_posix.cc \
    base/synchronization/waitable_event_posix.cc \
    base/sys_info.cc \
    base/sys_info_linux.cc \
    base/sys_info_posix.cc \
    base/threading/non_thread_safe_impl.cc \
    base/threading/platform_thread_linux.cc \
    base/threading/platform_thread_posix.cc \
    base/threading/simple_thread.cc \
    base/threading/thread_checker_impl.cc \
    base/threading/thread_collision_warner.cc \
    base/threading/thread_id_name_manager.cc \
    base/threading/thread_local_posix.cc \
    base/threading/thread_local_storage.cc \
    base/threading/thread_local_storage_posix.cc \
    base/threading/thread_restrictions.cc \
    base/threading/watchdog.cc \
    base/time/clock.cc \
    base/time/default_clock.cc \
    base/time/default_tick_clock.cc \
    base/time/tick_clock.cc \
    base/time/time.cc \
    base/time/time_posix.cc \
    base/version.cc \
    base/logging.cc \
    base/class_name.cpp \
    base/errno.cpp \
    base/find_cstr.cpp \
    base/status.cpp \
    base/string_printf.cpp \
    base/thread_local.cpp \
    base/unix_socket.cpp \
    base/endpoint.cpp \
    base/fd_utility.cpp \
    base/files/temp_file.cpp \
    base/files/file_watcher.cpp \
    base/time.cpp \
    base/zero_copy_stream_as_streambuf.cpp \
    base/crc32c.cc \
    base/containers/case_ignored_flat_map.cpp \
    base/iobuf.cpp

BASE_PROTOS = $(filter %.proto,$(BASE_SOURCES))
BASE_CFAMILIES = $(filter-out %.proto,$(BASE_SOURCES))
BASE_OBJS = $(BASE_PROTOS:.proto=.pb.o) $(addsuffix .o, $(basename $(BASE_CFAMILIES)))
BASE_DEBUG_OBJS = $(BASE_OBJS:.o=.dbg.o)

BVAR_DIRS = bvar bvar/detail
BVAR_SOURCES = $(foreach d,$(BVAR_DIRS),$(wildcard $(addprefix $(d)/*,$(SRCEXTS))))
BVAR_OBJS = $(addsuffix .o, $(basename $(BVAR_SOURCES))) 
BVAR_DEBUG_OBJS = $(BVAR_OBJS:.o=.dbg.o)

BTHREAD_DIRS = bthread
BTHREAD_SOURCES = $(foreach d,$(BTHREAD_DIRS),$(wildcard $(addprefix $(d)/*,$(SRCEXTS))))
BTHREAD_OBJS = $(addsuffix .o, $(basename $(BTHREAD_SOURCES))) 
BTHREAD_DEBUG_OBJS = $(BTHREAD_OBJS:.o=.dbg.o)

JSON2PB_DIRS = json2pb
JSON2PB_SOURCES = $(foreach d,$(JSON2PB_DIRS),$(wildcard $(addprefix $(d)/*,$(SRCEXTS))))
JSON2PB_OBJS = $(addsuffix .o, $(basename $(JSON2PB_SOURCES))) 
JSON2PB_DEBUG_OBJS = $(JSON2PB_OBJS:.o=.dbg.o)

BRPC_DIRS = brpc brpc/details brpc/builtin brpc/policy
BRPC_SOURCES = $(foreach d,$(BRPC_DIRS),$(wildcard $(addprefix $(d)/*,$(SRCEXTS))))
BRPC_PROTOS = $(filter %.proto,$(BRPC_SOURCES))
BRPC_CFAMILIES = $(filter-out %.proto brpc/policy/baidu_naming_service.cpp brpc/policy/giano_authenticator.cpp,$(BRPC_SOURCES))
BRPC_OBJS = $(BRPC_PROTOS:.proto=.pb.o) $(addsuffix .o, $(basename $(BRPC_CFAMILIES)))
BRPC_DEBUG_OBJS = $(BRPC_OBJS:.o=.dbg.o)

MCPACK2PB_SOURCES = \
	mcpack2pb/field_type.cpp \
	mcpack2pb/mcpack2pb.cpp \
	mcpack2pb/parser.cpp \
	mcpack2pb/serializer.cpp
MCPACK2PB_OBJS = idl_options.pb.o $(addsuffix .o, $(basename $(MCPACK2PB_SOURCES)))
MCPACK2PB_DEBUG_OBJS = $(MCPACK2PB_OBJS:.o=.dbg.o)

.PHONY:all
all: libbase.a libbvar.a libbthread.a libjson2pb.a libmcpack2pb.a protoc-gen-mcpack libbrpc.a output/include output/lib output/bin

.PHONY:debug
debug: libbase.dbg.a libbvar.dbg.a libbthread.dbg.a libjson2pb.dbg.a libmcpack2pb.dbg.a libbrpc.dbg.a

.PHONY:clean
clean:clean_debug
	@echo "Cleaning"
	@rm -rf libbase.a libbvar.a libbthread.a libjson2pb.a libmcpack2pb.a mcpack2pb/generator.o protoc-gen-mcpack libbrpc.a \
		$(BASE_OBJS) $(BVAR_OBJS) $(BTHREAD_OBJS) $(JSON2PB_OBJS) $(MCPACK2PB_OBJS) $(BRPC_OBJS) \
		output/include output/lib output/bin

.PHONY:clean_debug
clean_debug:
	@rm -rf libbase.dbg.a libbvar.dbg.a libbthread.dbg.a libjson2pb.dbg.a libmcpack2pb.dbg.a libbrpc.dbg.a \
		$(BASE_DEBUG_OBJS) $(BVAR_DEBUG_OBJS) $(BTHREAD_DEBUG_OBJS) $(JSON2PB_DEBUG_OBJS) $(MCPACK2PB_DEBUG_OBJS) $(BRPC_DEBUG_OBJS)

libbase.a:$(BASE_OBJS)
	@echo "Packing $@"
	@ar crs $@ $^

libbase.dbg.a:$(BASE_DEBUG_OBJS)
	@echo "Packing $@"
	@ar crs $@ $^

libbvar.a:$(BVAR_OBJS)
	@echo "Packing $@"
	@ar crs $@ $^

libbvar.dbg.a:$(BVAR_DEBUG_OBJS)
	@echo "Packing $@"
	@ar crs $@ $^

libbthread.a:$(BTHREAD_OBJS)
	@echo "Packing $@"
	@ar crs $@ $^

libbthread.dbg.a:$(BTHREAD_DEBUG_OBJS)
	@echo "Packing $@"
	@ar crs $@ $^

libjson2pb.a:$(JSON2PB_OBJS)
	@echo "Packing $@"
	@ar crs $@ $^

libjson2pb.dbg.a:$(JSON2PB_DEBUG_OBJS)
	@echo "Packing $@"
	@ar crs $@ $^

libmcpack2pb.a:$(MCPACK2PB_OBJS)
	@echo "Packing $@"
	@ar crs $@ $^

libmcpack2pb.dbg.a:$(MCPACK2PB_DEBUG_OBJS)
	@echo "Packing $@"
	@ar crs $@ $^

protoc-gen-mcpack:mcpack2pb/generator.o libmcpack2pb.a libbase.a libbthread.a libbvar.a
	@echo "Linking $@"
	@$(CXX) -o $@ $(LIBPATHS) -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic $(DYNAMIC_LINKINGS) -Xlinker "-)"

libbrpc.a:$(BRPC_OBJS)
	@echo "Packing $@"
	@ar crs $@ $^

libbrpc.dbg.a:$(BRPC_DEBUG_OBJS)
	@echo "Packing $@"
	@ar crs $@ $^

.PHONY:output/include
output/include:
	@echo "Copying to $@"
	@for dir in `find base bvar bthread brpc json2pb mcpack2pb -type f -name "*.h" -exec dirname {} \\; | sort | uniq`; do mkdir -p $@/$$dir && cp $$dir/*.h $@/$$dir/; done
	@for dir in `find base bvar bthread brpc json2pb mcpack2pb -type f -name "*.hpp" -exec dirname {} \\; | sort | uniq`; do mkdir -p $@/$$dir && cp $$dir/*.hpp $@/$$dir/; done
	@cp idl_options.proto idl_options.pb.h $@

.PHONY:output/lib
output/lib:libbase.a libbvar.a libbthread.a libjson2pb.a libmcpack2pb.a libbrpc.a
	@echo "Copying to $@"
	@mkdir -p $@
	@cp $^ $@

.PHONY:output/bin
output/bin:protoc-gen-mcpack
	@echo "Copying to $@"
	@mkdir -p $@
	@cp $^ $@

%.pb.cc:%.proto
	@echo "Generating $@"
	@$(PROTOC) --cpp_out=. --proto_path=. --proto_path=$(PROTOBUF_HDR) $<

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) -DNDEBUG $< -o $@

%.dbg.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) -DNDEBUG $< -o $@

%.dbg.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

%.o:%.c
	@echo "Compiling $@"
	@$(CC) -c $(HDRPATHS) $(CFLAGS) -DNDEBUG $< -o $@

%.dbg.o:%.c
	@echo "Compiling $@"
	@$(CC) -c $(HDRPATHS) $(CFLAGS) $< -o $@
