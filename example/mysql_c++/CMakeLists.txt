cmake_minimum_required(VERSION 3.16...3.28)
project(mysql_c++ C CXX)

include(${CMAKE_CURRENT_LIST_DIR}/../cmake/BrpcExample.cmake)

# Install dependencies:
# With apt:
# sudo apt-get install libreadline-dev
# sudo apt-get install ncurses-dev
# With yum:
# sudo yum install readline-devel
# sudo yum install ncurses-devel

option(LINK_SO "Whether examples are linked dynamically" OFF)

brpc_example_find_common_deps(DYNAMIC_LIB)

find_library(THRIFTNB_LIB NAMES thriftnb)
if(NOT THRIFTNB_LIB)
    set(THRIFTNB_LIB "")
endif()

list(APPEND DYNAMIC_LIB ${THRIFTNB_LIB})

set(AUX_LIB readline ncurses)

add_executable(mysql_cli mysql_cli.cpp)
brpc_example_configure_target(mysql_cli)
add_executable(mysql_tx mysql_tx.cpp)
brpc_example_configure_target(mysql_tx)
add_executable(mysql_stmt mysql_stmt.cpp)
brpc_example_configure_target(mysql_stmt)
add_executable(mysql_press mysql_press.cpp)
brpc_example_configure_target(mysql_press)
# add_executable(mysqlclient_press mysqlclient_press.cpp)

target_link_libraries(mysql_cli PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB} ${AUX_LIB})
target_link_libraries(mysql_tx PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB})
target_link_libraries(mysql_stmt PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB})
target_link_libraries(mysql_press PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB})
# target_link_libraries(mysqlclient_press PRIVATE ${BRPC_LIB} ${DYNAMIC_LIB})
