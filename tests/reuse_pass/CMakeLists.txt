cmake_minimum_required(VERSION 3.16)
project(portal_reuse_contracts LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
get_filename_component(PORTAL_SOURCE_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Threads REQUIRED)
enable_testing()

# Normal builds include the real export_callable/backend headers. The isolated
# mode is an explicit diagnostic fallback, not equivalent integration coverage.
option(PORTAL_REUSE_ISOLATED "Extract codec/router bodies without generated-backend dependencies" OFF)
option(PORTAL_REUSE_ALLOCATOR_EXPERIMENT "Build the rejected allocator experiment out of tree" OFF)
option(PORTAL_REUSE_NOEXCEPTION_TESTS "Add GCC/Clang strict=0/1 no-exception contracts" ON)

if(PORTAL_REUSE_ISOLATED)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${PORTAL_SOURCE_ROOT}/benchmarks/reuse_pass/extract_wire.py"
        "${PORTAL_SOURCE_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/wire/isolated_rpc.h"
        RESULT_VARIABLE extract_status)
    if(NOT extract_status EQUAL 0)
        message(FATAL_ERROR "isolated codec/router extraction failed")
    endif()
endif()

function(reuse_target target source)
    add_executable(${target} "${source}")
    target_include_directories(${target} PRIVATE "${PORTAL_SOURCE_ROOT}")
    target_link_libraries(${target} PRIVATE Threads::Threads)
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror)
    endif()
    if(PORTAL_REUSE_ISOLATED)
        target_compile_definitions(${target} PRIVATE PORTAL_REUSE_ISOLATED)
        target_include_directories(${target} PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/wire")
    endif()
    add_test(NAME ${target} COMMAND ${target})
    set_tests_properties(${target} PROPERTIES TIMEOUT 120 LABELS "contract;reuse")
endfunction()

if(NOT PORTAL_REUSE_ISOLATED)
    reuse_target(portal_rpc_reuse_generated_contract generated_binder_test.cpp)
endif()
reuse_target(portal_rpc_reuse_contract rpc_reuse_test.cpp)
reuse_target(portal_rpc_reuse_failure_contract allocation_failure_test.cpp)
set_tests_properties(portal_rpc_reuse_failure_contract PROPERTIES SKIP_RETURN_CODE 77)
reuse_target(portal_borrowed_decode_experiment borrowed_decode_test.cpp)
reuse_target(portal_async_cleanup_regression "${PORTAL_SOURCE_ROOT}/tests/async_cleanup.cpp")

if(PORTAL_REUSE_NOEXCEPTION_TESTS AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(strict 0 1)
        set(target portal_rpc_reuse_noexc_${strict})
        reuse_target(${target} rpc_reuse_test.cpp)
        target_compile_options(${target} PRIVATE -fno-exceptions)
        target_compile_definitions(${target} PRIVATE PORTAL_NO_EXCEPTION_STRICT=${strict})
    endforeach()
endif()

if(PORTAL_REUSE_ALLOCATOR_EXPERIMENT)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    find_package(Git REQUIRED)
    execute_process(COMMAND "${Python3_EXECUTABLE}"
        "${PORTAL_SOURCE_ROOT}/benchmarks/reuse_pass/prepare_allocator.py"
        "${PORTAL_SOURCE_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/allocator-overlay"
        RESULT_VARIABLE overlay_status)
    if(NOT overlay_status EQUAL 0)
        message(FATAL_ERROR "allocator experimental overlay preparation failed")
    endif()
    reuse_target(portal_async_allocator_experiment async_allocator_test.cpp)
    target_include_directories(portal_async_allocator_experiment BEFORE PRIVATE
        "${CMAKE_CURRENT_BINARY_DIR}/allocator-overlay")
endif()
