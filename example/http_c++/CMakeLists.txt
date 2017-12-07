include(FindProtobuf)
find_package(Protobuf REQUIRED)
include_directories(${PROTOBUF_INCLUDE_DIR})
protobuf_generate_cpp(PROTO_SRC PROTO_HEADER http.proto)

add_executable(http_client http_client.cpp)
target_link_libraries(http_client brpc)

add_executable(http_server http_server.cpp ${PROTO_SRC} ${PROTO_HEADER})
target_link_libraries(http_server brpc)

add_executable(benchmark_http benchmark_http.cpp)
target_link_libraries(benchmark_http brpc)
