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
    # project
    src/result.c
    src/view.c

    # Third party
    src/lib/stb_ds.c
)

# Add the test files
set(TEST_FILES
    test/test_result.c
    test/test_view.c
)

# ----- Project compilation ----- # 
add_executable(view_sync main.c ${SRC_FILES})
target_compile_options(view_sync PRIVATE -Wall -Wextra -Werror)

# ----- Project testing ----- # 
enable_testing()

# Iterate over test files and create individual executables
foreach(test_file ${TEST_FILES})
    get_filename_component(test_name "${test_file}" NAME_WE)

    add_executable(${test_name} ${test_file} ${SRC_FILES})
    target_link_libraries(${test_name} PRIVATE cmocka)
    target_compile_options(${test_name} PRIVATE -Wall -Wextra -Werror)
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
