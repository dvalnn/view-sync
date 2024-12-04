# Minimum CMake version
cmake_minimum_required(VERSION 3.10)
# Project name and version
project(view_sync VERSION 1.0 LANGUAGES C)

include_directories(include)

# Set C standard
set(CMAKE_C_STANDARD 23)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Add source files for the server
set(SRC_FILES
    src/udp_sim.c
    src/result.c
)

# Add the test files
set(TEST_FILES
    test/test_result.c
)

# ----- Project compilation ----- # 
add_executable(view_sync src/main.c ${SRC_FILES})

target_compile_options(view_sync PRIVATE -Wall -Wextra -Werror)

# ----- Project testing ----- # 
enable_testing()

add_executable(tests ${TEST_FILES} ${SRC_FILES})

target_link_libraries(tests PRIVATE cmocka)

target_compile_options(tests PRIVATE -Wall)

add_test(NAME view_sync_tests COMMAND tests)
