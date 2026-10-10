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

if(NOT DEFINED CXX_STANDARD)
    set(CXX_STANDARD 14)
endif()
separate_arguments(compiler_flags NATIVE_COMMAND "${CXX_FLAGS}")
message(STATUS "Codegen compiler: ${CXX}; flags: ${CXX_FLAGS}; C++${CXX_STANDARD}")
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

function(expect_schema_rejected name text diagnostic)
    set(directory "${WORK}/${name}")
    file(REMOVE_RECURSE "${directory}")
    file(MAKE_DIRECTORY "${directory}")
    file(WRITE "${directory}/echo.fbs" "${text}")
    execute_process(COMMAND "${FLATC}" --cpp -o "${directory}" "${directory}/echo.fbs"
        RESULT_VARIABLE flatc_result ERROR_VARIABLE flatc_error)
    if(NOT "${flatc_result}" STREQUAL "0")
        message(FATAL_ERROR "${name}: not a valid official flatc input: ${flatc_error}")
    endif()
    execute_process(COMMAND "${GENERATOR}" -o "${directory}" "${directory}/echo.fbs"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    string(FIND "${error}" "${diagnostic}" diagnostic_position)
    if("${result}" STREQUAL "0" OR diagnostic_position LESS 0)
        message(FATAL_ERROR "${name}: schema was accepted or lacked diagnostic: ${error}")
    endif()
    if(EXISTS "${directory}/echo.brpc.fb.h" OR EXISTS "${directory}/echo.brpc.fb.cpp")
        message(FATAL_ERROR "${name}: rejected schema emitted service files")
    endif()
    file(WRITE "${directory}/echo.brpc.fb.h" "previous header\n")
    file(WRITE "${directory}/echo.brpc.fb.cpp" "previous source\n")
    execute_process(COMMAND "${GENERATOR}" -o "${directory}"
        "${directory}/echo.fbs" RESULT_VARIABLE result ERROR_VARIABLE error)
    string(FIND "${error}" "${diagnostic}" diagnostic_position)
    file(READ "${directory}/echo.brpc.fb.h" old_header)
    file(READ "${directory}/echo.brpc.fb.cpp" old_source)
    if("${result}" STREQUAL "0" OR diagnostic_position LESS 0 OR
       NOT "${old_header}" STREQUAL "previous header\n" OR
       NOT "${old_source}" STREQUAL "previous source\n")
        message(FATAL_ERROR "${name}: rejection changed existing output files")
    endif()
    message(STATUS "Rejected ${name} without changing outputs: ${error}")
endfunction()

function(prepare_output_pair directory)
    file(REMOVE_RECURSE "${directory}")
    file(MAKE_DIRECTORY "${directory}")
    file(WRITE "${directory}/echo.fbs" "${schema_text}")
    file(WRITE "${directory}/echo.brpc.fb.h" "previous header\n")
    file(WRITE "${directory}/echo.brpc.fb.cpp" "previous source\n")
endfunction()

function(expect_output_pair_restored name failure)
    set(directory "${WORK}/${name}")
    prepare_output_pair("${directory}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "BRPC_FLATC_TEST_FAILURE=${failure}"
        "${GENERATOR}" -o "${directory}" "${directory}/echo.fbs"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if("${result}" STREQUAL "0" OR error STREQUAL "")
        message(FATAL_ERROR "${name}: injected failure was not reported")
    endif()
    file(READ "${directory}/echo.brpc.fb.h" current_header)
    file(READ "${directory}/echo.brpc.fb.cpp" current_source)
    if(NOT current_header STREQUAL "previous header\n" OR
       NOT current_source STREQUAL "previous source\n")
        message(FATAL_ERROR "${name}: previous output pair was not restored")
    endif()
    file(GLOB transaction_files "${directory}/echo.brpc.fb.*.tmp"
                                "${directory}/echo.brpc.fb.*.bak")
    if(transaction_files)
        message(FATAL_ERROR "${name}: transaction files remained after rollback")
    endif()
    message(STATUS "Restored output pair after ${failure}: ${error}")
endfunction()

function(expect_output_pair_removed name failure remaining_backup)
    set(directory "${WORK}/${name}")
    prepare_output_pair("${directory}")
    execute_process(COMMAND "${CMAKE_COMMAND}" -E env
        "BRPC_FLATC_TEST_FAILURE=${failure}"
        "${GENERATOR}" -o "${directory}" "${directory}/echo.fbs"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if("${result}" STREQUAL "0" OR error STREQUAL "")
        message(FATAL_ERROR "${name}: injected restore failure was not reported")
    endif()
    if(EXISTS "${directory}/echo.brpc.fb.h" OR EXISTS "${directory}/echo.brpc.fb.cpp")
        message(FATAL_ERROR "${name}: partial final output remained")
    endif()
    if(NOT EXISTS "${directory}/echo.brpc.fb.${remaining_backup}.bak")
        message(FATAL_ERROR "${name}: recoverable backup was not retained")
    endif()
    file(GLOB temporary_outputs "${directory}/echo.brpc.fb.*.tmp")
    if(temporary_outputs)
        message(FATAL_ERROR "${name}: temporary files remained")
    endif()
    message(STATUS "Removed final pair after ${failure}: ${error}")
endfunction()

function(expect_concurrent_output_pair)
    set(directory "${WORK}/concurrent_output_pair")
    set(output "${directory}/output")
    file(REMOVE_RECURSE "${directory}")
    file(MAKE_DIRECTORY "${directory}/first" "${directory}/second" "${output}")
    set(first_schema
        "namespace codegen.concurrent;\ntable Request {}\ntable Response {}\nrpc_service Echo { First(Request):Response (id: 1); }\n")
    set(second_schema
        "namespace codegen.concurrent;\ntable Request {}\ntable Response {}\nrpc_service Echo { Second(Request):Response (id: 2); }\n")
    file(WRITE "${directory}/first/echo.fbs" "${first_schema}")
    file(WRITE "${directory}/second/echo.fbs" "${second_schema}")
    file(WRITE "${directory}/run.sh"
        "#!/bin/sh\n\"${GENERATOR}\" -o \"${output}\" \"${directory}/first/echo.fbs\" &\nfirst=\$!\n\"${GENERATOR}\" -o \"${output}\" \"${directory}/second/echo.fbs\" &\nsecond=\$!\nwait \$first\nfirst_result=\$?\nwait \$second\nsecond_result=\$?\ntest \$first_result -eq 0 -a \$second_result -eq 0\n")
    execute_process(COMMAND /bin/sh "${directory}/run.sh"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "concurrent_output_pair: generation failed: ${error}")
    endif()
    execute_process(COMMAND "${FLATC}" --cpp -o "${output}"
        "${directory}/first/echo.fbs" RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "concurrent_output_pair: official flatc failed: ${error}")
    endif()
    set(includes "-I${output}")
    foreach(include_dir IN LISTS INCLUDE_DIRS)
        list(APPEND includes "-I${include_dir}")
    endforeach()
    execute_process(COMMAND "${CXX}" ${compiler_flags} "-std=c++${CXX_STANDARD}" ${includes}
        -c "${output}/echo.brpc.fb.cpp" -o "${directory}/echo.o"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "concurrent_output_pair: mixed generated files: ${error}")
    endif()
    file(GLOB transaction_files "${output}/echo.brpc.fb.*.tmp"
                                "${output}/echo.brpc.fb.*.bak")
    if(transaction_files)
        message(FATAL_ERROR "concurrent_output_pair: transaction files remained")
    endif()
    message(STATUS "Serialized concurrent output publication")
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
    set(standard "${CXX_STANDARD}")
    if(ARGC GREATER 2)
        set(standard "${ARGV2}")
    endif()
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
    set(diagnostics)
    foreach(include_dir IN LISTS INCLUDE_DIRS)
        if(ARGC GREATER 3)
            list(FIND IMPLICIT_INCLUDE_DIRS "${include_dir}" implicit_index)
            if(implicit_index GREATER_EQUAL 0)
                continue()
            endif()
            list(APPEND includes -isystem "${include_dir}")
        else()
            list(APPEND includes "-I${include_dir}")
        endif()
    endforeach()
    if(ARGC GREATER 3)
        set(diagnostics ${ARGV3})
    endif()
    execute_process(COMMAND "${CXX}" ${compiler_flags} ${diagnostics} "-std=c++${standard}" ${includes}
        -c "${directory}/echo.brpc.fb.cpp" -o "${directory}/echo.o"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "${name}: generated code did not compile: ${error}")
    endif()
    # The generated header must also compile without incidental prior includes.
    file(WRITE "${directory}/header.cpp" "#include \"echo.brpc.fb.h\"\n")
    execute_process(COMMAND "${CXX}" ${compiler_flags} ${diagnostics} "-std=c++${standard}" ${includes}
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

function(expect_distinct_headers name first_stem second_stem)
    set(directory "${WORK}/header_guards_${name}")
    set(includes)
    foreach(include_dir IN LISTS INCLUDE_DIRS)
        list(APPEND includes "-I${include_dir}")
    endforeach()
    foreach(side first second)
        set(stem "${${side}_stem}")
        set(output "${directory}/${side}")
        file(MAKE_DIRECTORY "${output}")
        string(REPLACE "namespace codegen.example;" "namespace guard_${name}.${side};"
            text "${schema_text}")
        # Isolate the fixture's global helper table as well as its services.
        string(PREPEND text "namespace guard_${name}.${side};\n")
        if(side STREQUAL "first")
            set(first_text "${text}")
        endif()
        file(WRITE "${output}/${stem}.fbs" "${text}")
        execute_process(COMMAND "${FLATC}" --cpp -o "${output}" "${output}/${stem}.fbs"
            RESULT_VARIABLE result ERROR_VARIABLE error)
        if(NOT "${result}" STREQUAL "0")
            message(FATAL_ERROR "${name}/${side}: official flatc failed: ${error}")
        endif()
        execute_process(COMMAND "${GENERATOR}" -o "${output}" "${output}/${stem}.fbs"
            RESULT_VARIABLE result ERROR_VARIABLE error)
        if(NOT "${result}" STREQUAL "0")
            message(FATAL_ERROR "${name}/${side}: brpc_flatc failed: ${error}")
        endif()
        file(WRITE "${output}/single.cpp"
            "#include \"${stem}.brpc.fb.h\"\n::guard_${name}::${side}::Echo* service = nullptr;\n")
        execute_process(COMMAND "${CXX}" ${compiler_flags} "-std=c++${CXX_STANDARD}" ${includes}
            -c "${output}/single.cpp" -o "${output}/single.o"
            RESULT_VARIABLE result ERROR_VARIABLE error)
        if(NOT "${result}" STREQUAL "0")
            message(FATAL_ERROR "${name}/${side}: standalone header failed: ${error}")
        endif()
    endforeach()
    foreach(reverse FALSE TRUE)
        set(first "#include \"first/${first_stem}.brpc.fb.h\"\n")
        set(second "#include \"second/${second_stem}.brpc.fb.h\"\n")
        if(reverse)
            set(headers "${second}${first}")
        else()
            set(headers "${first}${second}")
        endif()
        file(WRITE "${directory}/combined_${reverse}.cpp"
            "${headers}::guard_${name}::first::Echo* first_echo = nullptr;\n::guard_${name}::second::Echo* second_echo = nullptr;\n")
        execute_process(COMMAND "${CXX}" ${compiler_flags} "-std=c++${CXX_STANDARD}" ${includes}
            -c "${directory}/combined_${reverse}.cpp"
            -o "${directory}/combined_${reverse}.o"
            RESULT_VARIABLE result ERROR_VARIABLE error)
        if(NOT "${result}" STREQUAL "0")
            message(FATAL_ERROR "${name}: combined headers (reverse=${reverse}) failed: ${error}")
        endif()
    endforeach()
    # A checkout/output directory change must not alter generated identifiers.
    set(relocated "${directory}/relocated")
    file(MAKE_DIRECTORY "${relocated}")
    file(WRITE "${relocated}/${first_stem}.fbs" "${first_text}")
    execute_process(COMMAND "${GENERATOR}" -o . "${first_stem}.fbs"
        WORKING_DIRECTORY "${relocated}"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT "${result}" STREQUAL "0")
        message(FATAL_ERROR "${name}: relocated generation failed: ${error}")
    endif()
    foreach(suffix brpc.fb.h brpc.fb.cpp)
        file(READ "${directory}/first/${first_stem}.${suffix}" original)
        file(READ "${relocated}/${first_stem}.${suffix}" regenerated)
        if(NOT "${original}" STREQUAL "${regenerated}")
            message(FATAL_ERROR "${name}: ${suffix} depends on checkout/output paths")
        endif()
    endforeach()
    message(STATUS "Distinct, relocatable header guards: ${name}")
endfunction()

# Exercise the names emitted by official flatc, not only schema type names.
foreach(scope "" "api.")
    if(scope STREQUAL "")
        set(namespace_decl "")
        set(qualified "::")
        set(scope_id global)
    else()
        set(namespace_decl "namespace api;\n")
        set(qualified "::api::")
        set(scope_id nested)
    endif()
    set(tables "table Request {}\ntable Response {}\n")
    foreach(kind enum_value enum_min enum_any enum_values enum_names enum_name
                 union_traits union_verify union_vector table_create table_direct)
        if(kind MATCHES "^enum_")
            set(definitions "enum Collision: byte { Echo_Stub = 0 }\n")
            if(kind STREQUAL enum_value)
                set(service Collision_Echo)
                set(symbol Collision_Echo_Stub)
            elseif(kind STREQUAL enum_min)
                set(service Collision_MIN)
                set(symbol Collision_MIN)
            elseif(kind STREQUAL enum_any)
                set(definitions "enum Collision: byte (bit_flags) { Flag = 0 }\n")
                set(service Collision_ANY)
                set(symbol Collision_ANY)
            elseif(kind STREQUAL enum_values)
                set(service EnumValuesCollision)
                set(symbol EnumValuesCollision)
            elseif(kind STREQUAL enum_names)
                set(service EnumNamesCollision)
                set(symbol EnumNamesCollision)
            else()
                set(service EnumNameCollision)
                set(symbol EnumNameCollision)
            endif()
        elseif(kind MATCHES "^union_")
            set(definitions "union Collision { Request }\n")
            if(kind STREQUAL union_traits)
                set(service CollisionTraits)
            elseif(kind STREQUAL union_verify)
                set(service VerifyCollision)
            else()
                set(service VerifyCollisionVector)
            endif()
            set(symbol "${service}")
        elseif(kind STREQUAL table_create)
            set(definitions "table Echo_Stub {}\n")
            set(service CreateEcho)
            set(symbol CreateEcho_Stub)
        else()
            set(definitions "table Echo { text:string; }\n")
            set(service CreateEchoDirect)
            set(symbol CreateEchoDirect)
        endif()
        expect_schema_rejected(${scope_id}_${kind}
            "${namespace_decl}${tables}${definitions}rpc_service ${service} { Call(Request):Response (id: 1); }\n"
            "generated service class name collides: ${qualified}${symbol}")
    endforeach()
endforeach()

# Pair each root/helper declaration with actual flatc output and legal names.
foreach(symbol GetRoot_Stub GetSizePrefixedRoot_Stub VerifyRoot_StubBuffer
               VerifySizePrefixedRoot_StubBuffer FinishRoot_StubBuffer
               FinishSizePrefixedRoot_StubBuffer Root_StubIdentifier
               Root_StubBufferHasIdentifier SizePrefixedRoot_StubBufferHasIdentifier
               Root_StubExtension)
    expect_schema_rejected(root_helper_${symbol}
        "table Request {} table Response {} table Root_Stub {} root_type Root_Stub; file_identifier \"TEST\"; file_extension \"bin\"; rpc_service ${symbol} { Call(Request):Response (id: 1); }"
        "generated service class name collides: ::${symbol}")
endforeach()
file(WRITE "${WORK}/root_types.fbs"
    "namespace data; table Request {} table Response {} table Root_Stub {} root_type Root_Stub; file_identifier \"TEST\"; file_extension \"bin\";")
expect_schema_rejected(imported_root_helper
    "include \"../root_types.fbs\"; namespace data; rpc_service GetRoot { Call(Request):Response (id: 1); }"
    "generated service class name collides: ::data::GetRoot_Stub")
expect_schema_rejected(escaped_table_builder
    "table Request {} table Response {} table class {} rpc_service class_Builder { Call(Request):Response (id: 1); }"
    "generated service class name collides: ::class_Builder")
expect_compiles(unescaped_table_builder_available
    "table Request {} table Response {} table class {} rpc_service classBuilder { Call(Request):Response (id: 1); }")
expect_compiles(escaped_enum_value_available
    "table Request {} table Response {} enum E:byte { module = 0 } rpc_service E_module { Call(Request):Response (id: 1); }")
expect_compiles(sparse_enum_no_names_table
    "table Request {} table Response {} enum E:int { A = 0, B = 10 } rpc_service EnumNamesE { Call(Request):Response (id: 1); }")
expect_schema_rejected(dense_enum_names_table
    "table Request {} table Response {} enum E:int { A = 0, B = 9 } rpc_service EnumNamesE { Call(Request):Response (id: 1); }"
    "generated service class name collides: ::EnumNamesE")
expect_compiles(deprecated_string_no_direct
    "table Request {} table Response {} table Echo { text:string (deprecated); } rpc_service CreateEchoDirect { Call(Request):Response (id: 1); }")
expect_compiles(union_alias_no_traits
    "table Request {} table Response {} union E { A:Request, B:Request } rpc_service ETraits { Call(Request):Response (id: 1); }")
expect_compiles(nonroot_helper_available
    "table Request {} table Response {} table Root_Stub {} rpc_service GetRoot { Call(Request):Response (id: 1); }")

foreach(name __Echo Echo__Name _Echo _echo Echo_ channel_ owned_channel_)
    expect_schema_rejected(reserved_service_${name}
        "table Request {} table Response {} rpc_service ${name} { Call(Request):Response (id: 1); }"
        "reserved C++ identifier")
endforeach()
foreach(name __Call _Call Call__Name)
    expect_schema_rejected(reserved_method_${name}
        "table Request {} table Response {} rpc_service Echo { ${name}(Request):Response (id: 1); }"
        "reserved C++ identifier")
endforeach()
foreach(name __api _Api _api _)
    expect_schema_rejected(reserved_namespace_${name}
        "namespace ${name}; table Request {} table Response {} rpc_service Echo { Call(Request):Response (id: 1); }"
        "reserved C++ identifier")
endforeach()
expect_compiles(global_helper_table [=[
table BrpcFlatbuffersFail { value:int; }
table Request {} table Response {}
rpc_service Echo { Call(Request):Response (id: 1); }
]=])
expect_compiles(global_helper_service [=[
table Request {} table Response {}
rpc_service BrpcFlatbuffersFail { Call(Request):Response (id: 1); }
]=])
expect_compiles(legal_underscore_names [=[
namespace api._internal;
table Request {} table Response {}
rpc_service _echo { _call(Request):Response (id: 1); call_(Request):Response (id: 2); }
]=])
if(CXX_ID MATCHES "Clang")
    # Integer-only tables avoid unrelated reserved locals in upstream flatc.
    expect_compiles(strict_generated_identifiers
        "namespace api; table Request { value:int; } table Response { value:int; } rpc_service Echo { Call(Request):Response (id: 1); }"
        "${CXX_STANDARD}" "-Wreserved-identifier;-Werror=reserved-identifier")
else()
    message(STATUS "Reserved-name rejection tested; strict diagnostics require Clang")
endif()
expect_compiles(absent_direct_helper [=[
namespace api;
table Request {} table Response {} table Echo { value:int; }
rpc_service CreateEchoDirect { Call(Request):Response (id: 1); }
]=])
expect_compiles(cross_namespace_flatc_symbols [=[
namespace data;
table Request {} table Response {} enum Collision:byte { Echo_Stub = 0 }
namespace api;
rpc_service Collision_Echo { Call(data.Request):data.Response (id: 1); }
]=])

foreach(name Stub descriptor GetDescriptor FBCallMethod)
    string(CONCAT reserved_service_schema
        "table Request {}\ntable Response {}\n"
        "rpc_service ${name} { Call(Request):Response (id: 1); }\n")
    expect_schema_rejected(service_member_${name} "${reserved_service_schema}"
        "service name collides with generated API: ${name}")
    expect_schema_rejected(nested_service_member_${name}
        "namespace api;\n${reserved_service_schema}"
        "service name collides with generated API: ${name}")
endforeach()

foreach(name Echo Echo_Stub)
    foreach(depth "" ".nested")
        string(CONCAT namespace_collision_schema
            "namespace api.${name}${depth};\n"
            "table Request {}\ntable Response {}\n"
            "namespace api;\n"
            "rpc_service Echo {\n"
            "  Call(api.${name}${depth}.Request):"
            "api.${name}${depth}.Response (id: 1);\n}\n")
        expect_schema_rejected(namespace_collision_${name}${depth}
            "${namespace_collision_schema}"
            "generated service class name collides: ::api::${name}")
    endforeach()
endforeach()
file(WRITE "${WORK}/namespace_types.fbs"
    "namespace api.Echo; table Request {} table Response {}\n")
expect_schema_rejected(imported_namespace_collision
    "include \"../namespace_types.fbs\"; namespace api;
     rpc_service Echo {
       Call(api.Echo.Request):api.Echo.Response (id: 1);
     }"
    "generated service class name collides: ::api::Echo")

expect_distinct_headers(punctuation foo-bar foo_bar)
expect_distinct_headers(letter_case FooBar foobar)
expect_distinct_headers(same_basename echo echo)

expect_rejected_rpc_name(channel_)
expect_rejected_rpc_name(owned_channel_)
string(REPLACE "rpc_service Echo {" "rpc_service char8_t {"
    char8_t_schema "${schema_text}")
expect_schema_rejected(cpp20_char8_t "${char8_t_schema}"
    "C++ keyword is not supported: char8_t")
foreach(name brpc butil flatbuffers google std)
    set(global_namespace_collision_schema
        "table Request {}\ntable Response {}\nrpc_service ${name} { Call(Request):Response (id: 1); }\n")
    expect_schema_rejected(global_namespace_${name}
        "${global_namespace_collision_schema}"
        "generated service class name collides: ::${name}")
endforeach()
set(service_collision_schema
    "namespace codegen.collision;\ntable Request {}\ntable Response {}\nrpc_service Echo { Call(Request):Response (id: 1); }\nrpc_service Echo_Stub { Call(Request):Response (id: 2); }\n")
expect_schema_rejected(service_stub_collision "${service_collision_schema}"
    "generated service class name collides: ::codegen::collision::Echo_Stub")
set(stub_service_collision_schema
    "namespace codegen.collision;\ntable Request {}\ntable Response {}\nrpc_service Echo_Stub { Call(Request):Response (id: 2); }\nrpc_service Echo { Call(Request):Response (id: 1); }\n")
expect_schema_rejected(stub_service_collision "${stub_service_collision_schema}"
    "generated service class name collides: ::codegen::collision::Echo_Stub")
set(table_service_collision_schema
    "namespace codegen.collision;\ntable Request {}\ntable Response {}\ntable Echo {}\nrpc_service Echo { Call(Request):Response (id: 1); }\n")
expect_schema_rejected(table_service_collision "${table_service_collision_schema}"
    "generated service class name collides: ::codegen::collision::Echo")
set(table_stub_collision_schema
    "namespace codegen.collision;\ntable Request {}\ntable Response {}\ntable Echo_Stub {}\nrpc_service Echo { Call(Request):Response (id: 1); }\n")
expect_schema_rejected(table_stub_collision "${table_stub_collision_schema}"
    "generated service class name collides: ::codegen::collision::Echo_Stub")
set(table_builder_collision_schema
    "namespace codegen.collision;\ntable Request {}\ntable Response {}\ntable Echo {}\nrpc_service EchoBuilder { Call(Request):Response (id: 1); }\n")
expect_schema_rejected(table_builder_collision "${table_builder_collision_schema}"
    "generated service class name collides: ::codegen::collision::EchoBuilder")
set(enum_service_collision_schema
    "namespace codegen.collision;\ntable Request {}\ntable Response {}\nenum Echo : byte { Value = 0 }\nrpc_service Echo { Call(Request):Response (id: 1); }\n")
expect_schema_rejected(enum_service_collision "${enum_service_collision_schema}"
    "generated service class name collides: ::codegen::collision::Echo")
expect_output_pair_restored(stage_source_failure stage_source)
expect_output_pair_restored(preserve_source_failure preserve_source)
expect_output_pair_restored(publish_header_failure publish_header)
expect_output_pair_restored(publish_source_failure publish_source)
expect_output_pair_removed(restore_header_failure
    "publish_source,restore_header" h)
expect_output_pair_removed(restore_source_failure
    "publish_source,restore_source" cpp)
expect_concurrent_output_pair()
expect_rejected(missing_id "")
expect_rejected(duplicate_id "(id: 41)")
expect_rejected(negative_id "(id: -1)")
expect_rejected(overflow_id "(id: 2147483648)")
expect_rejected(string_id "(id: \"7\")")
expect_rejected(streaming "(id: 7, streaming: \"server\")")
expect_compiles(sparse_ids "${schema_text}")
file(WRITE "${WORK}/cxx20_probe.cpp" "int main() { return 0; }\n")
execute_process(COMMAND "${CXX}" ${compiler_flags} -std=c++20 -fsyntax-only "${WORK}/cxx20_probe.cpp"
    RESULT_VARIABLE cxx20_result ERROR_QUIET)
if("${cxx20_result}" STREQUAL "0")
    expect_compiles(cxx20 "${schema_text}" 20)
else()
    message(STATUS "Skipping C++20 compile smoke test: compiler lacks -std=c++20")
endif()
set(cross_namespace_schema
    "namespace codegen.common;\ntable Request {}\ntable Response {}\nnamespace codegen.first;\nrpc_service Echo { Call(codegen.common.Request):codegen.common.Response (id: 1); }\nnamespace codegen.second;\nrpc_service Echo_Stub { Call(codegen.common.Request):codegen.common.Response (id: 2); }\n")
expect_compiles(cross_namespace_names "${cross_namespace_schema}")
set(cross_namespace_type_schema
    "namespace codegen.common;\ntable Request {}\ntable Response {}\ntable Echo {}\nnamespace codegen.api;\nrpc_service Echo { Call(codegen.common.Request):codegen.common.Response (id: 1); }\n")
expect_compiles(cross_namespace_type_names "${cross_namespace_type_schema}")
expect_compiles(namespace_sibling [=[
namespace api.other.Echo;
table Request {}
table Response {}
namespace api;
rpc_service Echo {
  Call(api.other.Echo.Request):api.other.Echo.Response (id: 1);
}
]=])
expect_compiles(service_member_near_names [=[
namespace api;
table Request {}
table Response {}
rpc_service channel { Call(Request):Response (id: 1); }
rpc_service channel_handle { Call(Request):Response (id: 1); }
rpc_service owned_channel_handle { Call(Request):Response (id: 1); }
rpc_service StubService { Call(Request):Response (id: 1); }
rpc_service ChannelOwnership { Call(Request):Response (id: 1); }
]=])

set(shadow_names request response controller done method std fail BrpcFlatbuffersFail
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
    execute_process(COMMAND "${CXX}" ${compiler_flags} "-std=c++${CXX_STANDARD}" ${includes}
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

set(imported_collision_dir "${WORK}/imported_service_collision")
file(REMOVE_RECURSE "${imported_collision_dir}")
file(MAKE_DIRECTORY "${imported_collision_dir}")
file(WRITE "${imported_collision_dir}/types.fbs"
    "namespace shared; table Input {} table Output {}\n"
    "rpc_service Imported { Ping(Input):Output (id: 3); }\n")
file(WRITE "${imported_collision_dir}/echo.fbs"
    "include \"types.fbs\"; namespace shared;\n"
    "rpc_service Imported_Stub { Call(Input):Output (id: 7); }\n")
execute_process(COMMAND "${GENERATOR}" -o "${imported_collision_dir}"
    "${imported_collision_dir}/echo.fbs"
    RESULT_VARIABLE result ERROR_VARIABLE error)
if("${result}" STREQUAL "0" OR
   NOT error MATCHES "generated service class name collides: ::shared::Imported_Stub")
    message(FATAL_ERROR
        "imported_service_collision: schema was accepted or lacked diagnostic: ${error}")
endif()
if(EXISTS "${imported_collision_dir}/echo.brpc.fb.h" OR
   EXISTS "${imported_collision_dir}/echo.brpc.fb.cpp")
    message(FATAL_ERROR "imported_service_collision: rejected schema emitted files")
endif()
message(STATUS "Rejected imported service collision: ${error}")
