include ../../config.mk
BRPC_PATH = ../../output
CPPFLAGS = -std=c++0x -g -DNDEBUG -O2 -pipe -W -Wall -Werror -fPIC -Wno-deprecated -Wno-unused-parameter -fno-omit-frame-pointer -D__const__=
CXXFLAGS = $(CPPFLAGS)
INCPATH = -I$(BRPC_PATH)/include $(addprefix -I, $(INCS))
LIBPATH = $(addprefix -L, $(LIBS))
BRPC_LIBS = $(wildcard $(BRPC_PATH)/lib/lib*.a)
DYNAMIC_LINKING += -lreadline -lncurses

PRESS_SOURCES = redis_press.cpp
CLI_SOURCES = redis_cli.cpp

PRESS_OBJS = $(addsuffix .o, $(basename $(PRESS_SOURCES))) 
CLI_OBJS = $(addsuffix .o, $(basename $(CLI_SOURCES))) 
LDFLAGS = -Wl,-Bstatic $(STATIC_LINKING) -Wl,-Bdynamic $(DYNAMIC_LINKING)

.PHONY:all
all: redis_press redis_cli

.PHONY:clean
clean:
	@echo "Cleaning"
	@rm -rf redis_press redis_cli $(PRESS_OBJS) $(CLI_OBJS)

redis_press:$(PRESS_OBJS)
	@echo "Linking $@"
	@$(CXX) $(LIBPATH) -Xlinker "-(" $^ $(BRPC_LIBS) -Xlinker "-)" $(LDFLAGS) -o $@

redis_cli:$(CLI_OBJS)
	@echo "Linking $@"
	@$(CXX) $(LIBPATH) -Xlinker "-(" $^ $(BRPC_LIBS) -Xlinker "-)" $(LDFLAGS) -o $@

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(INCPATH) $(CXXFLAGS) $< -o $@
