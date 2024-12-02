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
    test/test2.c
)
# ----- Project compilation ----- # 
add_executable(view_sync ${SRC_FILES})

target_compile_options(view_sync PRIVATE -Wall -Wextra -Werror)

# ----- Project testing ----- # 
enable_testing()

foreach(TEST_FILE ${TEST_FILE})
    get_filename_component(TEST_NAME ${TEST_FILE} NAME_WE)
    
    add_executable(${TEST_NAME} ${TEST_FILE})

    target_link_libraries(${TEST_NAME} PRIVATE cmocka)

    target_compile_options(${TEST_NAME} PRIVATE -Wall -Wextra -Werror)

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endforeach()
