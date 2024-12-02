# Minimum CMake version
cmake_minimum_required(VERSION 3.10)

# Project name and version
project(UDPServerTest VERSION 1.0 LANGUAGES C)


include_directories(include)

# Set C standard
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED True)

# Add source files for the server
set(SRC_FILES
    src/main.c
)

# Add the test files
set(TEST_FILES
    test/test_main.c
)
# ----- Project compilation ----- # 
add_executable(view_sync ${SRC_FILES})

target_compile_options(view_sync PRIVATE -Wall -Wextra -Werror)

# ----- Project testing ----- # 
enable_testing()

add_executable(tests ${TEST_FILES})

target_link_libraries(tests PRIVATE cmocka)

target_compile_options(tests PRIVATE -Wall -Wextra -Werror)

add_test(NAME view_sync_tests COMMAND tests)
