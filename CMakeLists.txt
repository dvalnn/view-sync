# Minimum CMake version
cmake_minimum_required(VERSION 3.10)

# Project name and version
project(UDPServerTest VERSION 1.0 LANGUAGES C)

# Enable testing
enable_testing()

# Set C standard
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED True)

# Add source files for the server
set(SRC_FILES
    src/main.c
)

# Add the test files
set(TEST_FILES
    tests/test_main.c
)

# Find CMocka (required)
find_package(CMocka REQUIRED)

# Create an executable for the tests
add_executable(udp_server_tests ${TEST_FILES} ${SRC_FILES})

# Link CMocka library to the test executable
target_link_libraries(udp_server_tests PRIVATE cmocka)

# Add compile options (optional)
target_compile_options(udp_server_tests PRIVATE -Wall -Wextra -Werror)

# Add test to CTest
add_test(NAME UDPServerTests COMMAND udp_server_tests)
