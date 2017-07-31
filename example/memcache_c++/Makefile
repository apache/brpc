include ../../config.mk
BRPC_PATH = ../../output
CPPFLAGS = -std=c++0x -g -DNDEBUG -O2 -pipe -W -Wall -Werror -fPIC -Wno-deprecated -Wno-unused-parameter -D__const__=
CXXFLAGS = $(CPPFLAGS)
INCPATH = -I$(BRPC_PATH)/include $(addprefix -I, $(INCS))
LIBPATH = $(addprefix -L, $(LIBS))
BRPC_LIBS += $(wildcard $(BRPC_PATH)/lib/lib*.a)

SOURCES = $(wildcard *.cpp)
OBJS = $(addsuffix .o, $(basename $(SOURCES))) 
LDFLAGS = -Wl,-Bstatic $(STATIC_LINKING) -Wl,-Bdynamic $(DYNAMIC_LINKING)

.PHONY:all
all: memcache_client

.PHONY:clean
clean:
	@echo "Cleaning"
	@rm -rf memcache_client $(OBJS)

memcache_client:$(OBJS)
	@echo "Linking $@"
	@$(CXX) $(LIBPATH) -Xlinker "-(" $^ $(BRPC_LIBS) -Xlinker "-)" $(LDFLAGS) -o $@

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@
