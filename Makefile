NEED_LIBPROTOC=1
include config.mk

# Notes on the flags:
# 1. Added -fno-omit-frame-pointer: perf/tcmalloc-profiler use frame pointers by default
# 2. Added -D__const__= : Avoid over-optimizations of TLS variables by GCC>=4.8
# 3. Removed -Werror: Not block compilation for non-vital warnings, especially when the
#    code is tested on newer systems. If the code is used in production, add -Werror back
CPPFLAGS=-DBTHREAD_USE_FAST_PTHREAD_MUTEX -D__const__= -D_GNU_SOURCE -DUSE_SYMBOLIZE -DNO_TCMALLOC -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D__STDC_CONSTANT_MACROS -DBRPC_REVISION=\"$(shell git rev-parse --short HEAD)\"
CXXFLAGS=$(CPPFLAGS) -g -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-invalid-offsetof -Wno-unused-parameter -fno-omit-frame-pointer -std=c++0x -include brpc/config.h
CFLAGS=$(CPPFLAGS) -g -O2 -pipe -Wall -W -fPIC -fstrict-aliasing -Wno-unused-parameter -fno-omit-frame-pointer
HDRPATHS=-I./src $(addprefix -I, $(HDRS))
LIBPATHS = $(addprefix -L, $(LIBS))
SRCEXTS = .c .cc .cpp .proto
HDREXTS = .h .hpp

#required by base/crc32.cc to boost performance for 10x
ifeq ($(shell test $(GCC_VERSION) -ge 40400; echo $$?),0)
	CXXFLAGS+=-msse4 -msse4.2
endif
#not solved yet
ifeq ($(shell test $(GCC_VERSION) -ge 70000; echo $$?),0)
	CXXFLAGS+=-Wno-aligned-new
endif

BASE_SOURCES = \
    src/base/third_party/dmg_fp/g_fmt.cc \
    src/base/third_party/dmg_fp/dtoa_wrapper.cc \
    src/base/third_party/dynamic_annotations/dynamic_annotations.c \
    src/base/third_party/icu/icu_utf.cc \
    src/base/third_party/superfasthash/superfasthash.c \
    src/base/third_party/modp_b64/modp_b64.cc \
    src/base/third_party/nspr/prtime.cc \
    src/base/third_party/symbolize/demangle.cc \
    src/base/third_party/symbolize/symbolize.cc \
    src/base/third_party/xdg_mime/xdgmime.c \
    src/base/third_party/xdg_mime/xdgmimealias.c \
    src/base/third_party/xdg_mime/xdgmimecache.c \
    src/base/third_party/xdg_mime/xdgmimeglob.c \
    src/base/third_party/xdg_mime/xdgmimeicon.c \
    src/base/third_party/xdg_mime/xdgmimeint.c \
    src/base/third_party/xdg_mime/xdgmimemagic.c \
    src/base/third_party/xdg_mime/xdgmimeparent.c \
    src/base/third_party/xdg_user_dirs/xdg_user_dir_lookup.cc \
    src/base/third_party/snappy/snappy-sinksource.cc \
    src/base/third_party/snappy/snappy-stubs-internal.cc \
    src/base/third_party/snappy/snappy.cc \
    src/base/third_party/murmurhash3/murmurhash3.cpp \
    src/base/allocator/type_profiler_control.cc \
    src/base/arena.cpp \
    src/base/at_exit.cc \
    src/base/atomicops_internals_x86_gcc.cc \
    src/base/barrier_closure.cc \
    src/base/base_paths.cc \
    src/base/base_paths_posix.cc \
    src/base/base64.cc \
    src/base/base_switches.cc \
    src/base/big_endian.cc \
    src/base/bind_helpers.cc \
    src/base/callback_helpers.cc \
    src/base/callback_internal.cc \
    src/base/command_line.cc \
    src/base/cpu.cc \
    src/base/debug/alias.cc \
    src/base/debug/asan_invalid_access.cc \
    src/base/debug/crash_logging.cc \
    src/base/debug/debugger.cc \
    src/base/debug/debugger_posix.cc \
    src/base/debug/dump_without_crashing.cc \
    src/base/debug/proc_maps_linux.cc \
    src/base/debug/stack_trace.cc \
    src/base/debug/stack_trace_posix.cc \
    src/base/environment.cc \
    src/base/files/file.cc \
    src/base/files/file_posix.cc \
    src/base/files/file_enumerator.cc \
    src/base/files/file_enumerator_posix.cc \
    src/base/files/file_path.cc \
    src/base/files/file_path_constants.cc \
    src/base/files/memory_mapped_file.cc \
    src/base/files/memory_mapped_file_posix.cc \
    src/base/files/scoped_file.cc \
    src/base/files/scoped_temp_dir.cc \
    src/base/file_util.cc \
    src/base/file_util_linux.cc \
    src/base/file_util_posix.cc \
    src/base/guid.cc \
    src/base/guid_posix.cc \
    src/base/hash.cc \
    src/base/lazy_instance.cc \
    src/base/location.cc \
    src/base/md5.cc \
    src/base/memory/aligned_memory.cc \
    src/base/memory/ref_counted.cc \
    src/base/memory/ref_counted_memory.cc \
    src/base/memory/shared_memory_posix.cc \
    src/base/memory/singleton.cc \
    src/base/memory/weak_ptr.cc \
    src/base/nix/mime_util_xdg.cc \
    src/base/nix/xdg_util.cc \
    src/base/path_service.cc \
    src/base/posix/file_descriptor_shuffle.cc \
    src/base/posix/global_descriptors.cc \
    src/base/process/internal_linux.cc \
    src/base/process/kill.cc \
    src/base/process/kill_posix.cc \
    src/base/process/launch.cc \
    src/base/process/launch_posix.cc \
    src/base/process/process_handle_linux.cc \
    src/base/process/process_handle_posix.cc \
    src/base/process/process_info_linux.cc \
    src/base/process/process_iterator.cc \
    src/base/process/process_iterator_linux.cc \
    src/base/process/process_linux.cc \
    src/base/process/process_metrics.cc \
    src/base/process/process_metrics_linux.cc \
    src/base/process/process_metrics_posix.cc \
    src/base/process/process_posix.cc \
    src/base/rand_util.cc \
    src/base/rand_util_posix.cc \
    src/base/fast_rand.cpp \
    src/base/safe_strerror_posix.cc \
    src/base/sha1_portable.cc \
    src/base/strings/latin1_string_conversions.cc \
    src/base/strings/nullable_string16.cc \
    src/base/strings/safe_sprintf.cc \
    src/base/strings/string16.cc \
    src/base/strings/string_number_conversions.cc \
    src/base/strings/string_split.cc \
    src/base/strings/string_piece.cc \
    src/base/strings/string_util.cc \
    src/base/strings/string_util_constants.cc \
    src/base/strings/stringprintf.cc \
    src/base/strings/sys_string_conversions_posix.cc \
    src/base/strings/utf_offset_string_conversions.cc \
    src/base/strings/utf_string_conversion_utils.cc \
    src/base/strings/utf_string_conversions.cc \
    src/base/synchronization/cancellation_flag.cc \
    src/base/synchronization/condition_variable_posix.cc \
    src/base/synchronization/waitable_event_posix.cc \
    src/base/sys_info.cc \
    src/base/sys_info_linux.cc \
    src/base/sys_info_posix.cc \
    src/base/threading/non_thread_safe_impl.cc \
    src/base/threading/platform_thread_linux.cc \
    src/base/threading/platform_thread_posix.cc \
    src/base/threading/simple_thread.cc \
    src/base/threading/thread_checker_impl.cc \
    src/base/threading/thread_collision_warner.cc \
    src/base/threading/thread_id_name_manager.cc \
    src/base/threading/thread_local_posix.cc \
    src/base/threading/thread_local_storage.cc \
    src/base/threading/thread_local_storage_posix.cc \
    src/base/threading/thread_restrictions.cc \
    src/base/threading/watchdog.cc \
    src/base/time/clock.cc \
    src/base/time/default_clock.cc \
    src/base/time/default_tick_clock.cc \
    src/base/time/tick_clock.cc \
    src/base/time/time.cc \
    src/base/time/time_posix.cc \
    src/base/version.cc \
    src/base/logging.cc \
    src/base/class_name.cpp \
    src/base/errno.cpp \
    src/base/find_cstr.cpp \
    src/base/status.cpp \
    src/base/string_printf.cpp \
    src/base/thread_local.cpp \
    src/base/unix_socket.cpp \
    src/base/endpoint.cpp \
    src/base/fd_utility.cpp \
    src/base/files/temp_file.cpp \
    src/base/files/file_watcher.cpp \
    src/base/time.cpp \
    src/base/zero_copy_stream_as_streambuf.cpp \
    src/base/crc32c.cc \
    src/base/containers/case_ignored_flat_map.cpp \
    src/base/iobuf.cpp

BASE_OBJS = $(addsuffix .o, $(basename $(BASE_SOURCES)))

BVAR_DIRS = src/bvar src/bvar/detail
BVAR_SOURCES = $(foreach d,$(BVAR_DIRS),$(wildcard $(addprefix $(d)/*,$(SRCEXTS))))
BVAR_OBJS = $(addsuffix .o, $(basename $(BVAR_SOURCES))) 

BTHREAD_DIRS = src/bthread
BTHREAD_SOURCES = $(foreach d,$(BTHREAD_DIRS),$(wildcard $(addprefix $(d)/*,$(SRCEXTS))))
BTHREAD_OBJS = $(addsuffix .o, $(basename $(BTHREAD_SOURCES))) 

JSON2PB_DIRS = src/json2pb
JSON2PB_SOURCES = $(foreach d,$(JSON2PB_DIRS),$(wildcard $(addprefix $(d)/*,$(SRCEXTS))))
JSON2PB_OBJS = $(addsuffix .o, $(basename $(JSON2PB_SOURCES))) 

BRPC_DIRS = src/brpc src/brpc/details src/brpc/builtin src/brpc/policy
BRPC_SOURCES = $(foreach d,$(BRPC_DIRS),$(wildcard $(addprefix $(d)/*,$(SRCEXTS))))
BRPC_PROTOS = $(filter %.proto,$(BRPC_SOURCES))
BRPC_CFAMILIES = $(filter-out %.proto,$(BRPC_SOURCES))
BRPC_OBJS = $(BRPC_PROTOS:.proto=.pb.o) $(addsuffix .o, $(basename $(BRPC_CFAMILIES)))

MCPACK2PB_SOURCES = \
	src/mcpack2pb/field_type.cpp \
	src/mcpack2pb/mcpack2pb.cpp \
	src/mcpack2pb/parser.cpp \
	src/mcpack2pb/serializer.cpp
MCPACK2PB_OBJS = src/idl_options.pb.o $(addsuffix .o, $(basename $(MCPACK2PB_SOURCES)))

OBJS=$(BASE_OBJS) $(BVAR_OBJS) $(BTHREAD_OBJS) $(JSON2PB_OBJS) $(MCPACK2PB_OBJS) $(BRPC_OBJS)
DEBUG_OBJS = $(OBJS:.o=.dbg.o)

.PHONY:all
all:  protoc-gen-mcpack libbrpc.a output/include output/lib output/bin

.PHONY:debug
debug: libbrpc.dbg.a

.PHONY:clean
clean:clean_debug
	@echo "Cleaning"
	@rm -rf mcpack2pb/generator.o protoc-gen-mcpack libbrpc.a $(OBJS) output/include output/lib output/bin

.PHONY:clean_debug
clean_debug:
	@rm -rf libbrpc.dbg.a $(DEBUG_OBJS)

protoc-gen-mcpack:src/mcpack2pb/generator.o libbrpc.a
	@echo "Linking $@"
	@$(CXX) -o $@ $(LIBPATHS) -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic -Xlinker "-)" $(DYNAMIC_LINKINGS)

# force generation of pb headers before compiling to avoid fail-to-import issues in compiling pb.cc
libbrpc.a:$(BRPC_PROTOS:.proto=.pb.h) $(OBJS)
	@echo "Packing $@"
	@ar crs $@ $(OBJS)

libbrpc.dbg.a:$(BRPC_PROTOS:.proto=.pb.h) $(DEBUG_OBJS)
	@echo "Packing $@"
	@ar crs $@ $(DEBUG_OBJS)

.PHONY:output/include
output/include:
	@echo "Copying to $@"
	@for dir in `find src -type f -name "*.h" -exec dirname {} \\; | sed -e 's/^src\///g' -e '/^src$$/d' | sort | uniq`; do mkdir -p $@/$$dir && cp src/$$dir/*.h $@/$$dir/; done
	@for dir in `find src -type f -name "*.hpp" -exec dirname {} \\; | sed -e 's/^src\///g' -e '/^src$$/d' | sort | uniq`; do mkdir -p $@/$$dir && cp src/$$dir/*.hpp $@/$$dir/; done
	@cp src/idl_options.proto src/idl_options.pb.h $@

.PHONY:output/lib
output/lib:libbrpc.a
	@echo "Copying to $@"
	@mkdir -p $@
	@cp $^ $@

.PHONY:output/bin
output/bin:protoc-gen-mcpack
	@echo "Copying to $@"
	@mkdir -p $@
	@cp $^ $@

%.pb.cc %.pb.h:%.proto
	@echo "Generating $@"
	@$(PROTOC) --cpp_out=./src --proto_path=./src --proto_path=$(PROTOBUF_HDR) $<

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
