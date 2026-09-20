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

file(READ "${SCHEMA}" schema_text)
file(MAKE_DIRECTORY "${WORK}")

function(expect_rejected name replacement)
    set(directory "${WORK}/${name}")
    file(MAKE_DIRECTORY "${directory}")
    string(REPLACE "(id: 7)" "${replacement}" text "${schema_text}")
    file(WRITE "${directory}/echo.fbs" "${text}")
    execute_process(COMMAND "${GENERATOR}" -o "${directory}" "${directory}/echo.fbs"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if("${result}" STREQUAL "0" OR error STREQUAL "")
        message(FATAL_ERROR "${name}: invalid schema was accepted or lacked diagnostic")
    endif()
    if(EXISTS "${directory}/echo.brpc.fb.h" OR EXISTS "${directory}/echo.brpc.fb.cpp")
        message(FATAL_ERROR "${name}: invalid schema emitted service files")
    endif()
    message(STATUS "Rejected ${name}: ${error}")
endfunction()

function(expect_rejected_rpc_name name)
    set(directory "${WORK}/reserved_${name}")
    file(MAKE_DIRECTORY "${directory}")
    file(REMOVE "${directory}/echo.brpc.fb.h" "${directory}/echo.brpc.fb.cpp")
    string(REPLACE "Inspect(" "${name}(" text "${schema_text}")
    file(WRITE "${directory}/echo.fbs" "${text}")
    execute_process(COMMAND "${GENERATOR}" -o "${directory}" "${directory}/echo.fbs"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if("${result}" STREQUAL "0")
        message(SEND_ERROR "RPC ${name}: generator incorrectly accepted a fixed Stub field name")
        return()
    endif()
    if(NOT error MATCHES "method name collides with generated API: ${name}")
        message(FATAL_ERROR "RPC ${name}: expected a name-collision diagnostic: ${error}")
    endif()
    if(EXISTS "${directory}/echo.brpc.fb.h" OR EXISTS "${directory}/echo.brpc.fb.cpp")
        message(FATAL_ERROR "RPC ${name}: rejected schema emitted service files")
    endif()
    message(STATUS "Rejected RPC ${name}: ${error}")
endfunction()

function(expect_compiles name text)
    set(directory "${WORK}/${name}")
    file(MAKE_DIRECTORY "${directory}")
    file(WRITE "${directory}/echo.fbs" "${text}")
    execute_process(COMMAND "${FLATC}" --cpp -o "${directory}" "${directory}/echo.fbs"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "${name}: official flatc failed: ${error}")
    endif()
    execute_process(COMMAND "${GENERATOR}" -o "${directory}" "${directory}/echo.fbs"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "${name}: brpc_flatc failed: ${error}")
    endif()
    set(includes "-I${directory}")
    foreach(include_dir IN LISTS INCLUDE_DIRS)
        list(APPEND includes "-I${include_dir}")
    endforeach()
    execute_process(COMMAND "${CXX}" -std=c++14 ${includes}
        -c "${directory}/echo.brpc.fb.cpp" -o "${directory}/echo.o"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "${name}: generated code did not compile: ${error}")
    endif()
    # The generated header must also compile without incidental prior includes.
    file(WRITE "${directory}/header.cpp" "#include \"echo.brpc.fb.h\"\n")
    execute_process(COMMAND "${CXX}" -std=c++14 ${includes}
        -c "${directory}/header.cpp" -o "${directory}/header.o"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "${name}: generated header is not self-contained: ${error}")
    endif()
    foreach(suffix brpc.fb.h brpc.fb.cpp)
        file(READ "${directory}/echo.${suffix}" generated)
        if(NOT generated MATCHES "Licensed to the Apache Software Foundation")
            message(FATAL_ERROR "${name}: ${suffix} lacks Apache license")
        endif()
        if(generated MATCHES "FLATBUFFERS_VERSION")
            message(FATAL_ERROR "${name}: service output pins FlatBuffers version")
        endif()
    endforeach()
    message(STATUS "Compiled ${name}")
endfunction()

expect_rejected_rpc_name(channel_)
expect_rejected_rpc_name(owned_channel_)
expect_rejected(missing_id "")
expect_rejected(duplicate_id "(id: 41)")
expect_rejected(negative_id "(id: -1)")
expect_rejected(overflow_id "(id: 2147483648)")
expect_rejected(string_id "(id: \"7\")")
expect_rejected(streaming "(id: 7, streaming: \"server\")")
expect_compiles(sparse_ids "${schema_text}")

set(shadow_names request response controller done method std BrpcFlatbuffersFail
    STUB_OWNS_CHANNEL STUB_DOESNT_OWN_CHANNEL ChannelOwnership)
set(shadow_schema "namespace codegen.names;\ntable Request { text:string; }\ntable Response { text:string; }\nrpc_service Names {\n")
set(method_id 0)
foreach(name IN LISTS shadow_names)
    math(EXPR method_id "${method_id} + 7")
    string(APPEND shadow_schema "    ${name}(Request):Response (id: ${method_id});\n")
endforeach()
string(APPEND shadow_schema "}\nrpc_service ChannelOwnership { Ping(Request):Response (id: 3); }\n")
expect_compiles(shadowed_names "${shadow_schema}")

# Supply RUNTIME_LIBRARIES when invoking this script directly to exercise the
# new names against an existing FlatBuffers-enabled bRPC library as well.
if(RUNTIME_LIBRARIES)
    set(directory "${WORK}/shadowed_names")
    set(runtime_source [=[
#include "echo.brpc.fb.h"
#include <iostream>

using ::brpc::flatbuffers::Message;
using ::google::protobuf::Closure;
using ::google::protobuf::RpcController;

class Completion : public Closure {
public:
    void Run() override { ++runs; }
    int runs = 0;
};

class Implementation : public ::codegen::names::Names {
public:
    int selected = 0;
]=])
    set(method_id 0)
    foreach(name IN LISTS shadow_names)
        math(EXPR method_id "${method_id} + 7")
        string(APPEND runtime_source
            "    void ${name}(RpcController*, const Message*, Message*, Closure* completion) override { selected = ${method_id}; completion->Run(); }\n")
    endforeach()
    string(APPEND runtime_source [=[
};

class LocalChannel : public ::brpc::flatbuffers::RpcChannel {
public:
    explicit LocalChannel(::brpc::flatbuffers::Service* service) : service_(service) {}
    void FBCallMethod(const ::brpc::flatbuffers::MethodDescriptor* method,
                      RpcController* controller, const Message* request,
                      Message* response, Closure* done) override {
        service_->FBCallMethod(method, controller, request, response, done);
    }
private:
    ::brpc::flatbuffers::Service* service_;
};

int main() {
    ::brpc::flatbuffers::MessageBuilder builder;
    builder.Finish(::codegen::names::CreateRequest(builder));
    Message request = builder.ReleaseMessage();
    Message response;
    Implementation implementation;
    LocalChannel channel(&implementation);
    ::codegen::names::Names::Stub stub(&channel);
    ::codegen::names::Names defaults;
    LocalChannel default_channel(&defaults);
    ::codegen::names::Names::Stub default_stub(&default_channel);
    ::codegen::names::Names::Stub disconnected(nullptr);
    Completion completion;
    int expected_runs = 0;
]=])
    set(method_id 0)
    foreach(name IN LISTS shadow_names)
        math(EXPR method_id "${method_id} + 7")
        string(APPEND runtime_source
            "    stub.${name}(nullptr, &request, &response, &completion);\n"
            "    if (implementation.selected != ${method_id} || completion.runs != ++expected_runs) return 1;\n"
            "    default_stub.${name}(nullptr, &request, &response, &completion);\n"
            "    if (completion.runs != ++expected_runs) return 2;\n"
            "    disconnected.${name}(nullptr, &request, &response, &completion);\n"
            "    if (completion.runs != ++expected_runs) return 3;\n")
    endforeach()
    string(APPEND runtime_source [=[
    ::codegen::names::ChannelOwnership service;
    LocalChannel service_channel(&service);
    ::codegen::names::ChannelOwnership::Stub named_stub(&service_channel,
        ::brpc::flatbuffers::Service::STUB_DOESNT_OWN_CHANNEL);
    named_stub.Ping(nullptr, &request, &response, &completion);
    if (completion.runs != ++expected_runs) return 4;
    ::codegen::names::Names::Stub owned_stub(new LocalChannel(&implementation),
        ::brpc::flatbuffers::Service::STUB_OWNS_CHANNEL);
    owned_stub.request(nullptr, &request, &response, &completion);
    if (implementation.selected != 7 || completion.runs != ++expected_runs) return 5;
    std::cout << "Shadowed-name dispatch, failures, and constructors passed\n";
    return 0;
}
]=])
    file(WRITE "${directory}/runtime.cpp" "${runtime_source}")
    set(includes "-I${directory}")
    foreach(include_dir IN LISTS INCLUDE_DIRS)
        list(APPEND includes "-I${include_dir}")
    endforeach()
    execute_process(COMMAND "${CXX}" -std=c++14 ${includes}
        "${directory}/runtime.cpp" "${directory}/echo.o" ${RUNTIME_LIBRARIES}
        -o "${directory}/runtime"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "Shadowed-name runtime link failed: ${error}")
    endif()
    execute_process(COMMAND "${directory}/runtime"
        RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "Shadowed-name runtime failed: ${output}${error}")
    endif()
    message(STATUS "${output}")
endif()

string(REPLACE
    "    Repeat(Request):Response (id: 41);\n    Inspect(Request):Response (id: 7);"
    "    Inspect(Request):Response (id: 7);\n    Repeat(Request):Response (id: 41);"
    reordered "${schema_text}")
expect_compiles(reordered "${reordered}")
file(READ "${WORK}/reordered/echo.brpc.fb.cpp" reordered_source)
if(NOT reordered_source MATCHES "case 41:" OR NOT reordered_source MATCHES "case 7:")
    message(FATAL_ERROR "Reordering changed wire IDs")
endif()
string(FIND "${reordered_source}" "void Echo_Stub::Repeat(" repeat_begin)
if(repeat_begin LESS 0)
    message(FATAL_ERROR "Reordered stub lacks Repeat")
endif()
string(SUBSTRING "${reordered_source}" ${repeat_begin} -1 repeat_body)
string(FIND "${repeat_body}" "\n}\n" repeat_end)
string(SUBSTRING "${repeat_body}" 0 ${repeat_end} repeat_body)
if(NOT repeat_body MATCHES "method\\(1\\)")
    message(FATAL_ERROR "Reordered stub does not use its dense method position")
endif()

string(REPLACE "namespace codegen.example;" "" global_schema "${schema_text}")
expect_compiles(global_namespace "${global_schema}")
string(REPLACE "(id: 7)" "(id: 0)" zero_schema "${schema_text}")
expect_compiles(zero_id "${zero_schema}")
string(REPLACE "(id: 7)" "(id: 2147483647)" maximum_schema "${schema_text}")
expect_compiles(maximum_id "${maximum_schema}")

# Include schemas are read through the official Parser and not re-emitted.
file(MAKE_DIRECTORY "${WORK}/included")
file(WRITE "${WORK}/included/types.fbs"
    "namespace shared; table Input { note:string; } table Output { note:string; }\n"
    "rpc_service Imported { Ping(Input):Output (id: 3); }\n")
execute_process(COMMAND "${FLATC}" --cpp -o "${WORK}/included"
    "${WORK}/included/types.fbs" RESULT_VARIABLE result)
if(NOT "${result}" STREQUAL "0")
    message(FATAL_ERROR "Cannot generate included table definitions")
endif()
expect_compiles(included
    "include \"types.fbs\"; namespace api; rpc_service Local { Call(shared.Input):shared.Output (id: 17); }")
file(READ "${WORK}/included/echo.brpc.fb.h" included_header)
if(included_header MATCHES "class Imported")
    message(FATAL_ERROR "Included service was emitted twice")
endif()
