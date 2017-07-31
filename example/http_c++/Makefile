LINK_PERFTOOLS=1
include ../../config.mk
BRPC_PATH=../../output
CPPFLAGS=-std=c++0x -g -DNDEBUG -O2 -pipe -W -Wall -Werror -fPIC -Wno-deprecated -Wno-unused-parameter -D__const__= 
CXXFLAGS=$(CPPFLAGS)
CFLAGS=$(CPPFLAGS)
INCPATH = -I$(BRPC_PATH)/include $(addprefix -I, $(INCS))
LIBPATH = $(addprefix -L, $(LIBS))
BRPC_LIBS=$(wildcard $(BRPC_PATH)/lib/lib*.a)
ifeq ($(LINK_PERFTOOLS), 1)
	CPPFLAGS+=-DBRPC_ENABLE_CPU_PROFILER -DBRPC_ENABLE_HEAP_PROFILER
endif

CLIENT_SOURCES = http_client.cpp
BENCHMARK_SOURCES = benchmark_http.cpp
SERVER_SOURCES = http_server.cpp
PROTOS = http.proto

PROTO_OBJS = $(addsuffix .pb.o, $(basename $(PROTOS)))
PROTO_GENS = $(addsuffix .pb.h, $(basename $(PROTOS))) $(addsuffix .pb.cc, $(basename $(PROTOS)))
CLIENT_OBJS = $(addsuffix .o, $(basename $(CLIENT_SOURCES))) 
BENCHMARK_OBJS = $(addsuffix .o, $(basename $(BENCHMARK_SOURCES))) 
SERVER_OBJS = $(addsuffix .o, $(basename $(SERVER_SOURCES))) 
LDFLAGS = -Wl,-Bstatic $(STATIC_LINKING) -Wl,-Bdynamic $(DYNAMIC_LINKING)

.PHONY:all
all: http_client benchmark_http http_server

.PHONY:clean
clean:
	@echo "Cleaning"
	@rm -rf http_client benchmark_http http_server $(PROTO_GENS) $(PROTO_OBJS) $(CLIENT_OBJS) $(BENCHMARK_OBJS) $(SERVER_OBJS)

http_client:$(CLIENT_OBJS)
	@echo "Linking $@"
	@$(CXX) $(LIBPATH) -Xlinker "-(" $^ $(BRPC_LIBS) -Xlinker "-)" $(LDFLAGS) -o $@

benchmark_http:$(BENCHMARK_OBJS)
	@echo "Linking $@"
	@$(CXX) $(LIBPATH) -Xlinker "-(" $^ $(BRPC_LIBS) -Xlinker "-)" $(LDFLAGS) -o $@

http_server:$(PROTO_OBJS) $(SERVER_OBJS)
	@echo "Linking $@"
	@$(CXX) $(LIBPATH) -Xlinker "-(" $^ $(BRPC_LIBS) -Xlinker "-)" $(LDFLAGS) -o $@

%.pb.cc:%.proto
	@echo "Generating $@"
	@$(PROTOC) --cpp_out=. --proto_path=. $<

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@
