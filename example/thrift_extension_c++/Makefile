BRPC_PATH = ../../
include $(BRPC_PATH)/config.mk
# Notes on the flags:
# 1. Added -fno-omit-frame-pointer: perf/tcmalloc-profiler use frame pointers by default
# 2. Added -D__const__= : Avoid over-optimizations of TLS variables by GCC>=4.8
CXXFLAGS = -std=c++0x -g -DDEBUG -D__const__= -pipe -W -Wall -Werror -Wno-unused-parameter -fPIC -fno-omit-frame-pointer
HDRS+=$(BRPC_PATH)/output/include
LIBS+=$(BRPC_PATH)/output/lib
HDRPATHS = $(addprefix -I, $(HDRS))
LIBPATHS = $(addprefix -L, $(LIBS))
COMMA=,
SOPATHS=$(addprefix -Wl$(COMMA)-rpath=, $(LIBS))

STATIC_LINKINGS += -lbrpc -lits-thrift

CLIENT_SOURCES = client.cpp
SERVER_SOURCES = server.cpp
PROTOS = $(wildcard *.proto)

PROTO_OBJS = $(PROTOS:.proto=.pb.o)
PROTO_GENS = $(PROTOS:.proto=.pb.h) $(PROTOS:.proto=.pb.cc)
CLIENT_OBJS = $(addsuffix .o, $(basename $(CLIENT_SOURCES))) 
SERVER_OBJS = $(addsuffix .o, $(basename $(SERVER_SOURCES))) 

.PHONY:all
all: echo_client echo_server thrift_server thrift_client libechothrift.a

.PHONY:clean
clean:
	@echo "Cleaning"
	@rm -rf echo_client echo_server $(PROTO_GENS) $(PROTO_OBJS) $(CLIENT_OBJS) $(SERVER_OBJS) thrift_server thrift_client EchoService.o echo_types.o libechothrift.a

echo_client:$(PROTO_OBJS) $(CLIENT_OBJS) libechothrift.a
	@echo "Linking $@"
ifneq ("$(LINK_SO)", "")
	@$(CXX) $(LIBPATHS) $(SOPATHS) -Xlinker "-(" $^ -Xlinker "-)" $(STATIC_LINKINGS) $(DYNAMIC_LINKINGS) -o $@
else
	@$(CXX) $(LIBPATHS) -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic -Xlinker "-)" $(DYNAMIC_LINKINGS) -o $@
endif

echo_server:$(PROTO_OBJS) $(SERVER_OBJS) libechothrift.a
	@echo "Linking $@"
ifneq ("$(LINK_SO)", "")
	@$(CXX) $(LIBPATHS) $(SOPATHS) -Xlinker "-(" $^ libechothrift.a -Xlinker "-)" $(STATIC_LINKINGS) $(DYNAMIC_LINKINGS) -o $@
else
	@$(CXX) $(LIBPATHS) -Xlinker "-(" $^ libechothrift.a -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic -Xlinker "-)" $(DYNAMIC_LINKINGS) -o $@
endif

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

libechothrift.a:
	@echo "Generating thrift files"
	@/home/wangxuefeng/work/third-64/thrift/bin/thrift --gen cpp echo.thrift
	@g++ -c gen-cpp/echo_types.cpp -o echo_types.o -I/home/wangxuefeng/work/overall-eta-predictor/script/data/third-64-gcc485/thrift/include
	@g++ -c gen-cpp/EchoService.cpp -o EchoService.o -I/home/wangxuefeng/work/overall-eta-predictor/script/data/third-64-gcc485/thrift/include
	@ar -crv libechothrift.a EchoService.o echo_types.o

thrift_server: libechothrift.a
	@g++ thrift_server.cpp gen-cpp/echo_types.cpp gen-cpp/EchoService.cpp -I/home/wangxuefeng/work/overall-eta-predictor/script/data/third-64-gcc485/thrift/include -L/home/wangxuefeng/work/overall-eta-predictor/script/data/third-64-gcc485/thrift/lib -L/home/wangxuefeng/work/overall-eta-predictor/script/data/third-64-gcc485/libevent/lib -lthriftnb -lthrift -levent -lpthread -o thrift_server

thrift_client: libechothrift.a
	@g++ thrift_client.cpp gen-cpp/echo_types.cpp gen-cpp/EchoService.cpp -I/home/wangxuefeng/work/overall-eta-predictor/script/data/third-64-gcc485/thrift/include -L/home/wangxuefeng/work/overall-eta-predictor/script/data/third-64-gcc485/thrift/lib -lthriftnb -lthrift -levent -lpthread -o thrift_client
