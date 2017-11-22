project(example)

set(EchoClient_SOURCES
        ${CMAKE_SOURCE_DIR}/example/echo/echo.pb.cc
        ${CMAKE_SOURCE_DIR}/example/echo/client.cpp
        )
add_executable(EchoClient ${EchoClient_SOURCES})
target_link_libraries(EchoClient brpc)


set(EchoServer_SOURCES
        ${CMAKE_SOURCE_DIR}/example/echo/echo.pb.cc
        ${CMAKE_SOURCE_DIR}/example/echo/server.cpp
        )
add_executable(EchoServer ${EchoServer_SOURCES})
target_link_libraries(EchoServer brpc)

