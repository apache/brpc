include ../../config.mk
BRPC_PATH = ../../output
CC = gcc
CXX = g++
CPPFLAGS = -std=c++0x -g -DNDEBUG -O2 -pipe -W -Wall -Werror -fPIC -fno-omit-frame-pointer -Wno-deprecated -Wno-unused-parameter -D__const__=
CXXFLAGS = $(CPPFLAGS)
INCPATH = -I$(BRPC_PATH)/include -I$(GFLAGS_PATH)/include -I$(LEVELDB_PATH)/include -I$(PROTOBUF_PATH)/include
PROTOC = $(PROTOBUF_PATH)/bin/protoc
LIBS = $(wildcard $(BRPC_PATH)/lib/lib*.a) $(GFLAGS_PATH)/lib/libgflags.a $(LEVELDB_PATH)/lib/libleveldb.a $(PROTOBUF_PATH)/lib/libprotobuf.a \
	   -lpthread -lrt -lssl -lcrypto -ldl -lz

CLIENT_SOURCES = client.cpp
SERVER_SOURCES = server.cpp
PROTOS = echo.proto

PROTO_OBJS = $(addsuffix .pb.o, $(basename $(PROTOS)))
PROTO_GENS = $(addsuffix .pb.h, $(basename $(PROTOS))) $(addsuffix .pb.cc, $(basename $(PROTOS)))
CLIENT_OBJS = $(addsuffix .o, $(basename $(CLIENT_SOURCES))) 
SERVER_OBJS = $(addsuffix .o, $(basename $(SERVER_SOURCES))) 

.PHONY:all
all: echo_client echo_server

.PHONY:clean
clean:
	@echo "Cleaning"
	@rm -rf echo_client echo_server $(PROTO_GENS) $(PROTO_OBJS) $(CLIENT_OBJS) $(SERVER_OBJS)

echo_client:$(PROTO_OBJS) $(CLIENT_OBJS)
	@echo "Linking $@"
	@$(CXX) -Xlinker "-(" $^ $(LIBS) -Xlinker "-)" -o $@

echo_server:$(PROTO_OBJS) $(SERVER_OBJS)
	@echo "Linking $@"
	@$(CXX) -Xlinker "-(" $^ $(LIBS) -Xlinker "-)" -o $@

%.pb.cc:%.proto
	@echo "Generating $@"
	@$(PROTOC) --cpp_out=. --proto_path=. $<

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@
