cmake_minimum_required(VERSION 3.16)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(portal_typed_rpc_tests LANGUAGES CXX)
    enable_testing()
endif()
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
get_filename_component(PORTAL_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
find_package(Threads REQUIRED)
if(NOT Python3_Interpreter_FOUND)
    find_package(Python3 COMPONENTS Interpreter QUIET)
endif()
option(PORTAL_TYPED_RPC_BUILD_BENCHMARKS "Build typed RPC timing/allocation probes" ${PORTAL_BUILD_BENCHMARKS})
option(PORTAL_TYPED_RPC_ISOLATED_LEGACY "Extract legacy codec for benchmark diagnostics only" OFF)
option(PORTAL_TYPED_RPC_REQUIRE_FF "Require the real FF typed RPC integration test" OFF)
set(FLUX_FOUNDRY_INCLUDE_DIR "" CACHE PATH "FF checkout containing flow/flow.h")

function(portal_typed_rpc_test name source)
    set(target "${name}_test")
    add_executable(${target} "${source}")
    target_include_directories(${target} PRIVATE "${PORTAL_ROOT}")
    target_link_libraries(${target} PRIVATE Threads::Threads)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /WX)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Werror -pedantic)
        if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            target_compile_options(${target} PRIVATE -Wno-mismatched-new-delete)
        endif()
    endif()
    add_test(NAME ${name} COMMAND ${target})
    set_tests_properties(${name} PROPERTIES TIMEOUT 60 LABELS "typed_rpc")
endfunction()
portal_typed_rpc_test(typed_rpc_codec codec_test.cpp)
portal_typed_rpc_test(typed_rpc_async async_test.cpp)
portal_typed_rpc_test(typed_rpc_failures failure_test.cpp)
portal_typed_rpc_test(typed_rpc_contract_valid negative.cpp)
target_compile_definitions(typed_rpc_contract_valid_test PRIVATE NEGATIVE_CASE=0)

# Use CMake's configured toolchain, including MSVC's include/link environment.
# A valid control must compile before checking specific rejection diagnostics.
set(contract_diagnostics
    "field ids must be unique, nonzero and increasing"
    "wire field C.. type must match codec value_type"
    "wire record requires a PORTAL_WIRE_RECORD declaration"
    "typed RPC arguments must match the declared value types"
    "typed RPC handler must return the declared type exactly"
    "vector<bool> is not a wire sequence")
foreach(case RANGE 0 6)
    try_compile(contract_compiled
        "${CMAKE_CURRENT_BINARY_DIR}/compile_checks/${case}"
        "${CMAKE_CURRENT_SOURCE_DIR}/negative.cpp"
        CMAKE_FLAGS
            "-DCMAKE_CXX_STANDARD=14"
            "-DCMAKE_CXX_STANDARD_REQUIRED=ON"
            "-DCMAKE_CXX_EXTENSIONS=OFF"
            "-DINCLUDE_DIRECTORIES:STRING=${PORTAL_ROOT}"
        COMPILE_DEFINITIONS "-DNEGATIVE_CASE=${case}"
        OUTPUT_VARIABLE contract_output)
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/compile_checks/${case}.log" "${contract_output}")
    if(case EQUAL 0)
        if(NOT contract_compiled)
            message(FATAL_ERROR "Valid typed RPC contract failed to compile:\n${contract_output}")
        endif()
    else()
        math(EXPR diagnostic_index "${case} - 1")
        list(GET contract_diagnostics ${diagnostic_index} diagnostic)
        if(contract_compiled OR NOT contract_output MATCHES "${diagnostic}")
            message(FATAL_ERROR "Expected typed RPC diagnostic for case ${case}: ${diagnostic}\n${contract_output}")
        endif()
    endif()
endforeach()

if(Python3_Interpreter_FOUND)
    add_executable(typed_rpc_golden golden.cpp)
    target_include_directories(typed_rpc_golden PRIVATE "${PORTAL_ROOT}")
    add_test(NAME typed_rpc_python_golden COMMAND "${Python3_EXECUTABLE}"
        "${CMAKE_CURRENT_LIST_DIR}/golden.py" "$<TARGET_FILE:typed_rpc_golden>")
    set_tests_properties(typed_rpc_python_golden PROPERTIES TIMEOUT 60 LABELS "typed_rpc")
else()
    message(STATUS "Skipping typed RPC Python golden test: Python interpreter unavailable")
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    foreach(strict RANGE 0 1)
        foreach(kind codec async)
            set(name "typed_rpc_${kind}_noexcept_${strict}")
            portal_typed_rpc_test(${name} "${kind}_test.cpp")
            target_compile_options(${name}_test PRIVATE -fno-exceptions)
            target_compile_definitions(${name}_test PRIVATE PORTAL_NO_EXCEPTION_STRICT=${strict})
        endforeach()
    endforeach()
endif()
if(EXISTS "${FLUX_FOUNDRY_INCLUDE_DIR}/flow/flow.h")
    portal_typed_rpc_test(typed_rpc_ff ff_test.cpp)
    target_include_directories(typed_rpc_ff_test SYSTEM PRIVATE "${FLUX_FOUNDRY_INCLUDE_DIR}")
elseif(PORTAL_TYPED_RPC_REQUIRE_FF)
    message(FATAL_ERROR "Set FLUX_FOUNDRY_INCLUDE_DIR to a complete FF checkout for typed RPC integration")
else()
    message(STATUS "Skipping typed RPC FF integration: no FF checkout selected")
endif()
if(PORTAL_TYPED_RPC_BUILD_BENCHMARKS)
    if(PORTAL_TYPED_RPC_ISOLATED_LEGACY)
        if(NOT Python3_Interpreter_FOUND)
            message(FATAL_ERROR "Isolated legacy benchmark extraction requires Python")
        endif()
        execute_process(COMMAND "${Python3_EXECUTABLE}"
            "${PORTAL_ROOT}/benchmarks/reuse_pass/extract_wire.py"
            "${PORTAL_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/isolated_rpc.h"
            RESULT_VARIABLE code)
        if(NOT code EQUAL 0)
            message(FATAL_ERROR "Failed to extract the isolated legacy codec")
        endif()
    endif()
    foreach(target typed_probe typed_allocations)
        add_executable(${target} "${PORTAL_ROOT}/benchmarks/typed_rpc/probe.cpp")
        target_include_directories(${target} PRIVATE "${PORTAL_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}")
        target_link_libraries(${target} PRIVATE Threads::Threads)
        if(PORTAL_TYPED_RPC_ISOLATED_LEGACY)
            target_compile_definitions(${target} PRIVATE PORTAL_TYPED_RPC_ISOLATED_LEGACY=1)
        endif()
    endforeach()
    target_compile_definitions(typed_allocations PRIVATE PORTAL_TYPED_COUNT_ALLOCATIONS=1)
endif()
