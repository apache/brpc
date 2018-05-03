BRPC_PATH = ../../
include $(BRPC_PATH)/config.mk
CXXFLAGS = $(CPPFLAGS) -std=c++0x -DNDEBUG -O2 -D__const__= -pipe -W -Wall -fPIC -fno-omit-frame-pointer -Wno-unused-parameter
HDRPATHS = -I$(BRPC_PATH)/output/include $(addprefix -I, $(HDRS))
LIBPATHS = -L$(BRPC_PATH)/output/lib $(addprefix -L, $(LIBS))
STATIC_LINKINGS += $(BRPC_PATH)/output/lib/libbrpc.a

SOURCES = $(wildcard *.cpp)
OBJS = $(addsuffix .o, $(basename $(SOURCES))) 

.PHONY:all
all: parallel_http

.PHONY:clean
clean:
	@echo "Cleaning"
	@rm -rf parallel_http $(OBJS)

parallel_http:$(OBJS)
	@echo "Linking $@"
ifeq ($(SYSTEM),Linux)
	@$(CXX) $(LIBPATHS) -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic -Xlinker "-)" $(DYNAMIC_LINKINGS) -o $@
else ifeq ($(SYSTEM),Darwin)
	@$(CXX) $(LIBPATHS) $^ $(STATIC_LINKINGS) $(DYNAMIC_LINKINGS) -o $@
endif

%.o:%.cpp
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

%.o:%.cc
	@echo "Compiling $@"
	@$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@
