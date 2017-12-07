add_executable(redis_cli redis_cli.cpp)
target_link_libraries(redis_cli brpc)

add_executable(redis_press redis_press.cpp)
target_link_libraries(redis_press brpc)

