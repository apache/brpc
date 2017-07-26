CC=gcc
CXX=g++
CPPFLAGS=-std=c++0x -g -DNDEBUG -O2 -pipe -W -Wall -Werror -fPIC -fno-omit-frame-pointer -Wno-deprecated -Wno-unused-parameter -D__const__=
CXXFLAGS=$(CPPFLAGS)
CFLAGS=$(CPPFLAGS)
GFLAGS_PATH=../../../../../third-64/gflags
LEVELDB_PATH=../../../../../third-64/leveldb
PROTOBUF_PATH=../../../../../third-64/protobuf
BRPC_PATH=../../output
INCPATH=-I$(BRPC_PATH)/include -I$(GFLAGS_PATH)/include -I$(LEVELDB_PATH)/include -I$(PROTOBUF_PATH)/include
PROTOC=$(PROTOBUF_PATH)/bin/protoc
LIBS=$(BRPC_PATH)/lib/libbase.a \
  $(BRPC_PATH)/lib/libbrpc.a \
  $(BRPC_PATH)/lib/libbthread.a \
  $(BRPC_PATH)/lib/libbvar.a \
  $(BRPC_PATH)/lib/libjson2pb.a \
  $(BRPC_PATH)/lib/libmcpack2pb.a \
  $(GFLAGS_PATH)/lib/libgflags.a \
  $(LEVELDB_PATH)/lib/libleveldb.a \
  $(PROTOBUF_PATH)/lib/libprotobuf.a \
  -lpthread -lrt -lssl -lcrypto -ldl -lz

SOURCES = $(wildcard *.cpp)
OBJS = $(addsuffix .o, $(basename $(SOURCES))) 

.PHONY:all
all: rpc_replay

.PHONY:clean
clean:
	@echo "Cleaning"
	@rm -rf rpc_replay $(OBJS)

rpc_replay:$(OBJS)
	@echo "Linking $@"
	@$(CXX) -Xlinker "-(" $^ $(LIBS) -Xlinker "-)" -o $@

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@
