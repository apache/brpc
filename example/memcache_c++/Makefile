BRPC_PATH = ../../
include $(BRPC_PATH)/config.mk
CXXFLAGS = -std=c++0x -g -DNDEBUG -O2 -D__const__= -pipe -W -Wall -Werror -fPIC -fno-omit-frame-pointer
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
	@$(CXX) $(LIBPATHS) -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic -Xlinker "-)" $(DYNAMIC_LINKINGS) -o $@

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@
