cmake_minimum_required(VERSION 3.22)

project(test)

# Prepare source files

set(TEST_SOURCES test_helpers.hpp minos_tests.cpp ast_tests.cpp type_pool2_tests.cpp)

list(TRANSFORM INFRA_SOURCES PREPEND "../")

list(TRANSFORM CORE_SOURCES PREPEND "../")

list(TRANSFORM DIAG_SOURCES PREPEND "../")



# Main test program

prepare_exe(test test_main.cpp ${TEST_SOURCES} ${INFRA_SOURCES} ${CORE_SOURCES} ${DIAG_SOURCES})



# test-process-helper is a helper for testing `minos::process_create`.
# It gets copied into minos_fs_data/helpers/${COMPILER_NAME} so it can be run
# from test.

prepare_exe(test-process-helper test_process_helper.cpp ${INFRA_SOURCES})

target_compile_definitions(test PRIVATE HELPER_PROCESS_PATH="$<TARGET_FILE:test-process-helper>")



add_custom_target(run-tests COMMAND test --ignore-debugbreaks WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

add_dependencies(run-tests test test-process-helper)
