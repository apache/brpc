# Setup googletest
configure_file("${PROJECT_SOURCE_DIR}/cmake/CMakeLists.download_gtest.in" ${PROJECT_BINARY_DIR}/googletest-download/CMakeLists.txt)

execute_process(COMMAND ${CMAKE_COMMAND} -G "${CMAKE_GENERATOR}" .
                RESULT_VARIABLE result
                WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/googletest-download
)
if(result)
  message(FATAL_ERROR "CMake step for googletest failed: ${result}")
endif()

execute_process(COMMAND ${CMAKE_COMMAND} --build .
                RESULT_VARIABLE result
                WORKING_DIRECTORY ${PROJECT_BINARY_DIR}/googletest-download
)
if(result)
  message(FATAL_ERROR "Build step for googletest failed: ${result}")
endif()

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

add_subdirectory(${PROJECT_BINARY_DIR}/googletest-src
                 ${PROJECT_BINARY_DIR}/googletest-build
                 EXCLUDE_FROM_ALL)
