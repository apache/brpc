include ../../config_with_tcmalloc.mk
BRPC_PATH=../../output
CC=gcc
CXX=g++
CPPFLAGS=-std=c++0x -g -DNDEBUG -O2 -pipe -W -Wall -Werror -fPIC -fno-omit-frame-pointer -Wno-deprecated -Wno-unused-parameter -D__const__= -DBRPC_ENABLE_CPU_PROFILER -DBRPC_ENABLE_HEAP_PROFILER
CXXFLAGS=$(CPPFLAGS)
CFLAGS=$(CPPFLAGS)
INCPATH=-I$(BRPC_PATH)/include -I$(GFLAGS_PATH)/include -I$(LEVELDB_PATH)/include -I$(PROTOBUF_PATH)/include -I$(TCMALLOC_PATH)/include
PROTOC=$(PROTOBUF_PATH)/bin/protoc
LIBS=$(wildcard $(BRPC_PATH)/lib/lib*.a) $(GFLAGS_PATH)/lib/libgflags.a $(LEVELDB_PATH)/lib/libleveldb.a $(PROTOBUF_PATH)/lib/libprotobuf.a $(TCMALLOC_PATH)/lib/libtcmalloc_and_profiler.a \
	 -lpthread -lrt -lssl -lcrypto -ldl -lz

SOURCES = $(wildcard *.cpp)
OBJS = $(addsuffix .o, $(basename $(SOURCES))) 

.PHONY:all
all: memcache_client

.PHONY:clean
clean:
	@echo "Cleaning"
	@rm -rf memcache_client $(OBJS)

memcache_client:$(OBJS)
	@echo "Linking $@"
	@$(CXX) -Xlinker "-(" $^ $(LIBS) -Xlinker "-)" -o $@

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@
