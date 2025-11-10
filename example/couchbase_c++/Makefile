# Licensed to the Apache Software Foundation (ASF) under one
# or more contributor license agreements.  See the NOTICE file
# distributed with this work for additional information
# regarding copyright ownership.  The ASF licenses this file
# to you under the Apache License, Version 2.0 (the
# "License"); you may not use this file except in compliance
# with the License.  You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing,
# software distributed under the License is distributed on an
# "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
# KIND, either express or implied.  See the License for the
# specific language governing permissions and limitations
# under the License.

BRPC_PATH = ../../
include $(BRPC_PATH)/config.mk
CXXFLAGS+=$(CPPFLAGS) -std=c++17 -DNDEBUG -O2 -pipe -W -Wall -fPIC -fno-omit-frame-pointer
HDRS+=$(BRPC_PATH)/output/include
LIBS+=$(BRPC_PATH)/output/lib
HDRPATHS = $(addprefix -I, $(HDRS))
LIBPATHS = $(addprefix -L, $(LIBS))
COMMA=,
SOPATHS=$(addprefix -Wl$(COMMA)-rpath$(COMMA), $(LIBS))

# Define targets and their sources
TARGETS = couchbase_client multithreaded_couchbase_client traditional_brpc_couchbase_client
COUCHBASE_CLIENT_OBJS = couchbase_client.o
MULTITHREADED_CLIENT_OBJS = multithreaded_couchbase_client.o
TRADITIONAL_CLIENT_OBJS = traditional_brpc_couchbase_client.o
ALL_OBJS = $(COUCHBASE_CLIENT_OBJS) $(MULTITHREADED_CLIENT_OBJS) $(TRADITIONAL_CLIENT_OBJS)

ifeq ($(SYSTEM),Darwin)
 ifneq ("$(LINK_SO)", "")
	STATIC_LINKINGS += -lbrpc
 else
	# *.a must be explicitly specified in clang
	STATIC_LINKINGS += $(BRPC_PATH)/output/lib/libbrpc.a
 endif
	LINK_OPTIONS_SO = $^ $(STATIC_LINKINGS) $(DYNAMIC_LINKINGS)
	LINK_OPTIONS = $^ $(STATIC_LINKINGS) $(DYNAMIC_LINKINGS)
else ifeq ($(SYSTEM),Linux)
	STATIC_LINKINGS += -lbrpc
	LINK_OPTIONS_SO = -Xlinker "-(" $^ -Xlinker "-)" $(STATIC_LINKINGS) $(DYNAMIC_LINKINGS)
	LINK_OPTIONS = -Xlinker "-(" $^ -Wl,-Bstatic $(STATIC_LINKINGS) -Wl,-Bdynamic -Xlinker "-)" $(DYNAMIC_LINKINGS)
endif

.PHONY: all clean couchbase_client multithreaded_couchbase_client help

# Default target builds both clients
all: $(TARGETS)

clean:
	@echo "> Cleaning"
	rm -rf $(TARGETS) $(ALL_OBJS)

# Build rules for individual targets
couchbase_client: $(COUCHBASE_CLIENT_OBJS)
	@echo "> Linking $@"
ifneq ("$(LINK_SO)", "")
	$(CXX) $(LIBPATHS) $(SOPATHS) $(LINK_OPTIONS_SO) -o $@
else
	$(CXX) $(LIBPATHS) $(LINK_OPTIONS) -o $@
endif

multithreaded_couchbase_client: $(MULTITHREADED_CLIENT_OBJS)
	@echo "> Linking $@"
ifneq ("$(LINK_SO)", "")
	$(CXX) $(LIBPATHS) $(SOPATHS) $(LINK_OPTIONS_SO) -o $@
else
	$(CXX) $(LIBPATHS) $(LINK_OPTIONS) -o $@
endif

traditional_brpc_couchbase_client: $(TRADITIONAL_CLIENT_OBJS)
	@echo "> Linking $@"
ifneq ("$(LINK_SO)", "")
	$(CXX) $(LIBPATHS) $(SOPATHS) $(LINK_OPTIONS_SO) -o $@
else
	$(CXX) $(LIBPATHS) $(LINK_OPTIONS) -o $@
endif

# Compilation rules
couchbase_client.o: couchbase_client.cpp
	@echo "> Compiling $@"
	$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

multithreaded_couchbase_client.o: multithreaded_couchbase_client.cpp
	@echo "> Compiling $@"
	$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@

traditional_brpc_couchbase_client.o: traditional_brpc_couchbase_client.cpp
	@echo "> Compiling $@"
	$(CXX) -c $(HDRPATHS) $(CXXFLAGS) $< -o $@