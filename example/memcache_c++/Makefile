include ../../config.mk
BRPC_PATH = ../../output
CPPFLAGS = -std=c++0x -g -DNDEBUG -O2 -pipe -W -Wall -Werror -fPIC -Wno-deprecated -Wno-unused-parameter -D__const__=
CXXFLAGS = $(CPPFLAGS)
INCPATH = -I$(BRPC_PATH)/include $(addprefix -I, $(INCS))
LIBS += $(wildcard $(BRPC_PATH)/lib/lib*.a)
LDFLAGS = -lpthread -lrt -lssl -lcrypto -ldl -lz

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
	@$(CXX) -Xlinker "-(" $^ $(LIBS) -Xlinker "-)" $(LDFLAGS) -o $@

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@
