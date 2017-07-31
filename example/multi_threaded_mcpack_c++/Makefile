LINK_PERFTOOLS=1
include ../../config.mk
BRPC_PATH=../../output
CPPFLAGS=-std=c++0x -g -DNDEBUG -O2 -pipe -W -Wall -Werror -fPIC -Wno-deprecated -Wno-unused-parameter -D__const__=
CXXFLAGS=$(CPPFLAGS)
CFLAGS=$(CPPFLAGS)
INCPATH=-I$(BRPC_PATH)/include $(addprefix -I, $(INCS))
LIBPATH = $(addprefix -L, $(LIBS))
BRPC_LIBS=$(wildcard $(BRPC_PATH)/lib/lib*.a)
ifeq ($(LINK_PERFTOOLS), 1)
	CPPFLAGS += -DBRPC_ENABLE_CPU_PROFILER -DBRPC_ENABLE_HEAP_PROFILER
endif

CLIENT_SOURCES = client.cpp
SERVER_SOURCES = server.cpp
PROTOS = echo.proto

PROTO_OBJS = $(addsuffix .pb.o, $(basename $(PROTOS)))
PROTO_GENS = $(addsuffix .pb.h, $(basename $(PROTOS))) $(addsuffix .pb.cc, $(basename $(PROTOS)))
CLIENT_OBJS = $(addsuffix .o, $(basename $(CLIENT_SOURCES))) 
SERVER_OBJS = $(addsuffix .o, $(basename $(SERVER_SOURCES))) 
LDFLAGS = -Wl,-Bstatic $(STATIC_LINKING) -Wl,-Bdynamic $(DYNAMIC_LINKING)

.PHONY:all
all: echo_client echo_server

.PHONY:clean
clean:
	@echo "Cleaning"
	@rm -rf echo_client echo_server $(PROTO_GENS) $(PROTO_OBJS) $(CLIENT_OBJS) $(SERVER_OBJS)

echo_client:$(PROTO_OBJS) $(CLIENT_OBJS)
	@echo "Linking $@"
	@$(CXX) $(LIBPATH) -Xlinker "-(" $^ $(BRPC_LIBS) -Xlinker "-)" $(LDFLAGS) -o $@

echo_server:$(PROTO_OBJS) $(SERVER_OBJS)
	@echo "Linking $@"
	@$(CXX) $(LIBPATH) -Xlinker "-(" $^ $(BRPC_LIBS) -Xlinker "-)" $(LDFLAGS) -o $@

%.pb.cc:%.proto
	@echo "Generating $@"
	@$(PROTOC) --plugin=protoc-gen-mcpack=$(BRPC_PATH)/bin/protoc-gen-mcpack --proto_path=. --proto_path=$(BRPC_PATH)/include --proto_path=$(PROTOBUF_INC) --cpp_out=. --mcpack_out=. $<

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@
