BRPC_PATH = ../../
include $(BRPC_PATH)/config.mk
CXXFLAGS = -std=c++0x -g -DNDEBUG -O2 -D__const__= -pipe -W -Wall -Werror -fPIC -fno-omit-frame-pointer
HDRPATHS = -I$(BRPC_PATH)/output/include $(addprefix -I, $(HDRS))
LIBPATHS = -L$(BRPC_PATH)/output/lib $(addprefix -L, $(LIBS))
STATIC_LINKINGS += -lbase -lbvar -lbthread -lbrpc -ljson2pb -lmcpack2pb

CLIENT_SOURCES = client.cpp
SERVER_SOURCES = server.cpp
PROTOS = $(wildcard *.proto)

PROTO_OBJS = $(PROTOS:.proto=.pb.o)
PROTO_GENS = $(PROTOS:.proto=.pb.h) $(PROTOS:.proto=.pb.cc)
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
	@$(CXX) $(LIBPATHS) -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic -Xlinker "-)" $(DYNAMIC_LINKINGS) -o $@

echo_server:$(PROTO_OBJS) $(SERVER_OBJS)
	@echo "Linking $@"
	@$(CXX) $(LIBPATHS) -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic -Xlinker "-)" $(DYNAMIC_LINKINGS) -o $@

%.pb.cc:%.proto
	@echo "Generating $@"
	@$(PROTOC) --cpp_out=. --proto_path=. $(PROTOC_EXTRA_ARGS) $<

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@
