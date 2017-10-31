BRPC_PATH = ../../
include $(BRPC_PATH)/config.mk
CXXFLAGS+=$(CPPFLAGS) -std=c++0x -DNDEBUG -O2 -D__const__= -pipe -W -Wall -fPIC -fno-omit-frame-pointer
HDRS+=$(BRPC_PATH)/output/include
LIBS+=$(BRPC_PATH)/output/lib
HDRPATHS = $(addprefix -I, $(HDRS))
LIBPATHS = $(addprefix -L, $(LIBS))
COMMA=,
SOPATHS=$(addprefix -Wl$(COMMA)-rpath=, $(LIBS))

DYNAMIC_LINKINGS += -lreadline -lncurses
STATIC_LINKINGS += -lbrpc

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
ifneq ("$(LINK_SO)", "")
	@$(CXX) $(LIBPATHS) $(SOPATHS) -Xlinker "-(" $^ -Xlinker "-)" $(STATIC_LINKINGS) $(DYNAMIC_LINKINGS) -o $@
else
	@$(CXX) $(LIBPATHS) -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic -Xlinker "-)" $(DYNAMIC_LINKINGS) -o $@
endif

redis_cli:$(CLI_OBJS)
	@echo "Linking $@"
ifneq ("$(LINK_SO)", "")
	@$(CXX) $(LIBPATHS) $(SOPATHS) -Xlinker "-(" $^ -Xlinker "-)" $(STATIC_LINKINGS) $(DYNAMIC_LINKINGS) -o $@
else
	@$(CXX) $(LIBPATHS) -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic -Xlinker "-)" $(DYNAMIC_LINKINGS) -o $@
endif

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@
