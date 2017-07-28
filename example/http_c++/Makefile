LINK_PERFTOOLS=1
include ../../config.mk
BRPC_PATH=../../output
CPPFLAGS=-std=c++0x -g -DNDEBUG -O2 -pipe -W -Wall -Werror -fPIC -Wno-deprecated -Wno-unused-parameter -D__const__= 
CXXFLAGS=$(CPPFLAGS)
CFLAGS=$(CPPFLAGS)
INCPATH=-I$(BRPC_PATH)/include -I$(GFLAGS_PATH)/include -I$(LEVELDB_PATH)/include -I$(PROTOBUF_PATH)/include -I$(TCMALLOC_PATH)/include
LIBS+=$(wildcard $(BRPC_PATH)/lib/lib*.a)
LDFLAGS=-lpthread -lrt -lssl -lcrypto -ldl -lz
ifeq ($(LINK_PERFTOOLS), 1)
	LIBS+=-DBRPC_ENABLE_CPU_PROFILER -DBRPC_ENABLE_HEAP_PROFILER
	LDFLAGS+=-llzma
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

.PHONY:all
all: http_client benchmark_http http_server

.PHONY:clean
clean:
	@echo "Cleaning"
	@rm -rf http_client benchmark_http http_server $(PROTO_GENS) $(PROTO_OBJS) $(CLIENT_OBJS) $(BENCHMARK_OBJS) $(SERVER_OBJS)

http_client:$(CLIENT_OBJS)
	@echo "Linking $@"
	@$(CXX) -Xlinker "-(" $^ $(LIBS) -Xlinker "-)" $(LDFLAGS) -o $@

benchmark_http:$(BENCHMARK_OBJS)
	@echo "Linking $@"
	@$(CXX) -Xlinker "-(" $^ $(LIBS) -Xlinker "-)" $(LDFLAGS) -o $@

http_server:$(PROTO_OBJS) $(SERVER_OBJS)
	@echo "Linking $@"
	@$(CXX) -Xlinker "-(" $^ $(LIBS) -Xlinker "-)" $(LDFLAGS) -o $@

%.pb.cc:%.proto
	@echo "Generating $@"
	@$(PROTOC) --cpp_out=. --proto_path=. $<

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@
