CC=gcc
CXX=g++
CPPFLAGS=-std=c++0x -g -DNDEBUG -O2 -pipe -W -Wall -Werror -fPIC -fno-omit-frame-pointer -Wno-deprecated -Wno-unused-parameter -D__const__= -DBRPC_ENABLE_CPU_PROFILER -DBRPC_ENABLE_HEAP_PROFILER
CXXFLAGS=$(CPPFLAGS)
CFLAGS=$(CPPFLAGS)
GFLAGS_PATH=../../../../../third-64/gflags
LEVELDB_PATH=../../../../../third-64/leveldb
PROTOBUF_PATH=../../../../../third-64/protobuf
TCMALLOC_PATH=../../../../../third-64/tcmalloc
BRPC_PATH=../../output
INCPATH=-I$(BRPC_PATH)/include -I$(GFLAGS_PATH)/include -I$(LEVELDB_PATH)/include -I$(PROTOBUF_PATH)/include -I$(TCMALLOC_PATH)/include
LIBS=$(BRPC_PATH)/lib/libbase.a \
  $(BRPC_PATH)/lib/libbrpc.a \
  $(BRPC_PATH)/lib/libbthread.a \
  $(BRPC_PATH)/lib/libbvar.a \
  $(BRPC_PATH)/lib/libjson2pb.a \
  $(BRPC_PATH)/lib/libmcpack2pb.a \
  $(GFLAGS_PATH)/lib/libgflags.a \
  $(LEVELDB_PATH)/lib/libleveldb.a \
  $(PROTOBUF_PATH)/lib/libprotobuf.a \
  $(TCMALLOC_PATH)/lib/libtcmalloc_and_profiler.a \
  -lpthread -lrt -lssl -lcrypto -ldl -lz -lreadline -lncurses

PRESS_SOURCES = redis_press.cpp
CLI_SOURCES = redis_cli.cpp

PRESS_OBJS = $(addsuffix .o, $(basename $(PRESS_SOURCES))) 
CLI_OBJS = $(addsuffix .o, $(basename $(CLI_SOURCES))) 

.PHONY:all
all: redis_press redis_cli

.PHONY:clean
clean:
	@echo "Cleaning"
	@rm -rf redis_press redis_cli $(PRESS_OBJS) $(CLI_OBJS)

redis_press:$(PRESS_OBJS)
	@echo "Linking $@"
	@$(CXX) -Xlinker "-(" $^ $(LIBS) -Xlinker "-)" -o $@

redis_cli:$(CLI_OBJS)
	@echo "Linking $@"
	@$(CXX) -Xlinker "-(" $^ $(LIBS) -Xlinker "-)" -o $@

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@
