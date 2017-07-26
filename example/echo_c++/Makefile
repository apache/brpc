CC=gcc
CXX=g++
CXXFLAGS=
CFLAGS=
CPPFLAGS=-std=c++0x \
  -g \
  -DNDEBUG \
  -O2 \
  -pipe \
  -W \
  -Wall \
  -Werror \
  -fPIC \
  -fno-omit-frame-pointer \
  -Wno-deprecated \
  -Wno-unused-parameter \
  -D__const__=
INCPATH=-I../../
GFLAGS_PATH=../../../../../third-64/gflags
LEVELDB_PATH=../../../../../third-64/leveldb
PROTOBUF_PATH=../../../../../third-64/protobuf
DEP_INCPATH=-I$(GFLAGS_PATH)/include -I$(LEVELDB_PATH)/include -I$(PROTOBUF_PATH)/include
PROTOC=$(PROTOBUF_PATH)/bin/protoc
LIBS=../../output/lib/libbase.a \
  ../../output/lib/libbrpc.a \
  ../../output/lib/libbthread.a \
  ../../output/lib/libbvar.a \
  ../../output/lib/libjson2pb.a \
  ../../output/lib/libmcpack2pb.a \
  $(GFLAGS_PATH)/lib/libgflags.a \
  $(LEVELDB_PATH)/lib/libleveldb.a \
  $(PROTOBUF_PATH)/lib/libprotobuf.a \
  -lpthread \
  -lrt \
  -lssl \
  -lcrypto \
  -ldl \
  -lz

.PHONY:all
all: echo_client echo_server

.PHONY:clean
clean:
	rm -rf echo_client
	rm -rf echo_server
	rm -rf echo.pb.cc
	rm -rf echo.pb.h
	rm -rf echo.pb.o
	rm -rf client.o
	rm -rf server.o

echo_client:echo.pb.o client.o
	$(CXX) echo.pb.o client.o -Xlinker "-(" $(LIBS) -Xlinker "-)" -o echo_client

echo_server:echo.pb.o server.o
	$(CXX) echo.pb.o server.o -Xlinker "-(" $(LIBS) -Xlinker "-)" -o echo_server

echo.pb.o:echo.pb.cc
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o echo.pb.o echo.pb.cc

echo.pb.cc echo.pb.h:echo.proto
	$(PROTOC) --cpp_out=. --proto_path=.  echo.proto

client.o:client.cpp
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o client.o client.cpp

server.o:server.cpp
	$(CXX) -c $(INCPATH) $(DEP_INCPATH) $(CPPFLAGS) $(CXXFLAGS)  -o server.o server.cpp
