cmake_minimum_required(VERSION 3.13)

project(view_sync)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(include)

add_executable(view_sync src/main.c)
add_executable(tests tests/test_runner.c tests/test_main.c)

find_package(CMocka REQUIRED)
target_link_libraries(tests ${CMOCKA_LIBRARY})
