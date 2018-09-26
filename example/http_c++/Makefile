NEED_GPERFTOOLS=1
BRPC_PATH=../../
include $(BRPC_PATH)/config.mk
# Notes on the flags:
# 1. Added -fno-omit-frame-pointer: perf/tcmalloc-profiler use frame pointers by default
# 2. Added -D__const__= : Avoid over-optimizations of TLS variables by GCC>=4.8
CXXFLAGS+=$(CPPFLAGS) -std=c++0x -DNDEBUG -O2 -D__const__= -pipe -W -Wall -Wno-unused-parameter -fPIC -fno-omit-frame-pointer
ifeq ($(NEED_GPERFTOOLS), 1)
	CXXFLAGS+=-DBRPC_ENABLE_CPU_PROFILER
endif
HDRS+=$(BRPC_PATH)/output/include
LIBS+=$(BRPC_PATH)/output/lib
HDRPATHS = $(addprefix -I, $(HDRS))
LIBPATHS = $(addprefix -L, $(LIBS))
COMMA=,
SOPATHS=$(addprefix -Wl$(COMMA)-rpath$(COMMA), $(LIBS))

CLIENT_SOURCES = http_client.cpp
BENCHMARK_SOURCES = benchmark_http.cpp
SERVER_SOURCES = http_server.cpp
PROTOS = http.proto

PROTO_OBJS = $(PROTOS:.proto=.pb.o)
PROTO_GENS = $(PROTOS:.proto=.pb.h) $(PROTOS:.proto=.pb.cc)
CLIENT_OBJS = $(addsuffix .o, $(basename $(CLIENT_SOURCES))) 
BENCHMARK_OBJS = $(addsuffix .o, $(basename $(BENCHMARK_SOURCES))) 
SERVER_OBJS = $(addsuffix .o, $(basename $(SERVER_SOURCES))) 

ifeq ($(SYSTEM),Darwin)
 ifneq ("$(LINK_SO)", "")
	STATIC_LINKINGS += -lbrpc
 else
	# *.a must be explicitly specified in clang
	STATIC_LINKINGS += $(BRPC_PATH)/output/lib/libbrpc.a
 endif
	LINK_OPTIONS_SO = $^ $(STATIC_LINKINGS) $(DYNAMIC_LINKINGS)
	LINK_OPTIONS = $^ $(STATIC_LINKINGS) $(DYNAMIC_LINKINGS)
else ifeq ($(SYSTEM),Linux)
	STATIC_LINKINGS += -lbrpc
	LINK_OPTIONS_SO = -Xlinker "-(" $^ -Xlinker "-)" $(STATIC_LINKINGS) $(DYNAMIC_LINKINGS)
	LINK_OPTIONS = -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic -Xlinker "-)" $(DYNAMIC_LINKINGS)
endif

.PHONY:all
all: http_client benchmark_http http_server

.PHONY:clean
clean:
	@echo "Cleaning"
	@rm -rf http_client benchmark_http http_server $(PROTO_GENS) $(PROTO_OBJS) $(CLIENT_OBJS) $(BENCHMARK_OBJS) $(SERVER_OBJS)

http_client:$(CLIENT_OBJS)
	@echo "Linking $@"
ifneq ("$(LINK_SO)", "")
	@$(CXX) $(LIBPATHS) $(SOPATHS) $(LINK_OPTIONS_SO) -o $@
else
	@$(CXX) $(LIBPATHS) $(LINK_OPTIONS) -o $@
endif

benchmark_http:$(BENCHMARK_OBJS)
	@echo "Linking $@"
ifneq ("$(LINK_SO)", "")
	@$(CXX) $(LIBPATHS) $(SOPATHS) $(LINK_OPTIONS_SO) -o $@
else
	@$(CXX) $(LIBPATHS) $(LINK_OPTIONS) -o $@
endif

http_server:$(PROTO_OBJS) $(SERVER_OBJS)
	@echo "Linking $@"
ifneq ("$(LINK_SO)", "")
	@$(CXX) $(LIBPATHS) $(SOPATHS) $(LINK_OPTIONS_SO) -o $@
else
	@$(CXX) $(LIBPATHS) $(LINK_OPTIONS) -o $@
endif

%.pb.cc %.pb.h:%.proto
	@echo "Generating $@"
	@$(PROTOC) --cpp_out=. --proto_path=. $<

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@
