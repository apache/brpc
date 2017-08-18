BRPC_PATH = ../../
include $(BRPC_PATH)/config.mk
CPPFLAGS = -std=c++0x -g -DNDEBUG -O2 -pipe -W -Wall -Werror -fPIC -Wno-deprecated -Wno-unused-parameter -fno-omit-frame-pointer -D__const__=
CXXFLAGS = $(CPPFLAGS)
HDRPATHS = -I$(BRPC_PATH)/output/include $(addprefix -I, $(HDRS))
LIBPATHS = -L$(BRPC_PATH)/output/lib $(addprefix -L, $(LIBS))
STATIC_LINKINGS += -lbase -lbvar -lbthread -lbrpc -ljson2pb -lmcpack2pb

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
	@$(CXX) $(LIBPATHS) -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic $(DYNAMIC_LINKINGS) -Xlinker "-)" -o $@

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@
