NEED_GPERFTOOLS=1
BRPC_PATH=../../
include $(BRPC_PATH)/config.mk
CXXFLAGS=-std=c++0x -g -DNDEBUG -O2 -D__const__= -pipe -W -Wall -Werror -fPIC -fno-omit-frame-pointer
ifeq ($(NEED_GPERFTOOLS), 1)
	CXXFLAGS+=-DBRPC_ENABLE_CPU_PROFILER -DBRPC_ENABLE_HEAP_PROFILER
endif
HDRPATHS = -I$(BRPC_PATH)/output/include $(addprefix -I, $(HDRS))
LIBPATHS = -L$(BRPC_PATH)/output/lib $(addprefix -L, $(LIBS))
STATIC_LINKINGS += -lbase -lbvar -lbthread -lbrpc -ljson2pb -lmcpack2pb

CLIENT_SOURCES = http_client.cpp
BENCHMARK_SOURCES = benchmark_http.cpp
SERVER_SOURCES = http_server.cpp
PROTOS = http.proto

PROTO_OBJS = $(PROTOS:.proto=.pb.o)
PROTO_GENS = $(PROTOS:.proto=.pb.h) $(PROTOS:.proto=.pb.cc)
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
	@$(CXX) $(LIBPATHS) -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic -Xlinker "-)" $(DYNAMIC_LINKINGS) -o $@

benchmark_http:$(BENCHMARK_OBJS)
	@echo "Linking $@"
	@$(CXX) $(LIBPATHS) -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic -Xlinker "-)" $(DYNAMIC_LINKINGS) -o $@

http_server:$(PROTO_OBJS) $(SERVER_OBJS)
	@echo "Linking $@"
	@$(CXX) $(LIBPATHS) -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic -Xlinker "-)" $(DYNAMIC_LINKINGS) -o $@

%.pb.cc:%.proto
	@echo "Generating $@"
	@$(PROTOC) --cpp_out=. --proto_path=. $<

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@
