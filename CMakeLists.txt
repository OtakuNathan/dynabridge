cmake_minimum_required(VERSION 3.16)

project(dynabridge)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(${PROJECT_NAME} main.cpp)
target_include_directories(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

enable_testing()

add_executable(fake_backend_callable_group_test
    tests/fake_backend_callable_group.cpp
)
target_include_directories(fake_backend_callable_group_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME fake_backend_callable_group COMMAND fake_backend_callable_group_test)

add_executable(custom_def_config_test
    tests/custom_def_config_test.cpp
)
target_include_directories(custom_def_config_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME custom_def_config COMMAND custom_def_config_test)

add_executable(optional_test
    tests/optional_test.cpp
)
target_include_directories(optional_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
add_test(NAME optional COMMAND optional_test)

find_package(Python3 COMPONENTS Interpreter Development)
if(Python3_Development_FOUND)
    add_executable(python_backend_test
        tests/python_backend_test.cpp
    )
    target_include_directories(python_backend_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(python_backend_test PRIVATE Python3::Python)
    add_test(NAME python_backend COMMAND python_backend_test)

    add_library(python_runtime_addon MODULE
        tests/python_runtime_addon.cpp
    )
    set_target_properties(python_runtime_addon PROPERTIES
        PREFIX ""
        OUTPUT_NAME "dynabridge_python_runtime_addon"
    )
    if(WIN32)
        set_target_properties(python_runtime_addon PROPERTIES
            SUFFIX ".pyd"
        )
    endif()
    target_include_directories(python_runtime_addon PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        ${Python3_INCLUDE_DIRS}
    )
    if(APPLE)
        target_link_options(python_runtime_addon PRIVATE
            "-undefined"
            "dynamic_lookup"
        )
    else()
        target_link_libraries(python_runtime_addon PRIVATE Python3::Python)
    endif()
    add_test(
        NAME python_runtime
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_runtime_smoke.py"
            "$<TARGET_FILE:python_runtime_addon>"
    )
endif()

option(DYNABRIDGE_BUILD_BENCHMARKS "Build dynabridge benchmark executables" OFF)

if(DYNABRIDGE_BUILD_BENCHMARKS AND Python3_Development_FOUND)
    add_executable(python_call_benchmark
        benchmarks/python_call_benchmark.cpp
    )
    target_include_directories(python_call_benchmark PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(python_call_benchmark PRIVATE Python3::Python)

    find_package(pybind11 CONFIG QUIET)
    if(pybind11_FOUND)
        target_compile_definitions(python_call_benchmark PRIVATE
            DYNABRIDGE_HAS_PYBIND11=1
        )
        target_link_libraries(python_call_benchmark PRIVATE pybind11::headers)
    endif()

    find_package(Python COMPONENTS Interpreter Development QUIET)
    if(Python_FOUND)
        execute_process(
            COMMAND "${Python_EXECUTABLE}" -m nanobind --cmake_dir
            RESULT_VARIABLE DYNABRIDGE_NANOBIND_CMAKE_DIR_RESULT
            OUTPUT_VARIABLE DYNABRIDGE_NANOBIND_CMAKE_DIR
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET
        )
        if(DYNABRIDGE_NANOBIND_CMAKE_DIR_RESULT EQUAL 0)
            find_package(nanobind CONFIG QUIET
                PATHS "${DYNABRIDGE_NANOBIND_CMAKE_DIR}"
                NO_DEFAULT_PATH
            )
        endif()
    endif()
    if(nanobind_FOUND)
        nanobind_build_library(nanobind-static)
        set_property(TARGET python_call_benchmark PROPERTY
            CXX_STANDARD 17
        )
        target_compile_definitions(python_call_benchmark PRIVATE
            DYNABRIDGE_HAS_NANOBIND=1
        )
        target_link_libraries(python_call_benchmark PRIVATE nanobind-static)
    endif()
endif()

add_executable(napi_backend_test
    tests/napi_backend_test.cpp
)
target_include_directories(napi_backend_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/napi_stub"
)
add_test(NAME napi_backend COMMAND napi_backend_test)

find_program(Node_EXECUTABLE NAMES node nodejs)
set(NodeApi_INCLUDE_HINTS)
set(NodeApi_LIBRARY_HINTS)
if(Node_EXECUTABLE)
    get_filename_component(Node_EXECUTABLE_DIR "${Node_EXECUTABLE}" DIRECTORY)
    list(APPEND NodeApi_INCLUDE_HINTS
        "${Node_EXECUTABLE_DIR}/../include/node"
        "${Node_EXECUTABLE_DIR}/include/node"
    )
    list(APPEND NodeApi_LIBRARY_HINTS
        "${Node_EXECUTABLE_DIR}"
        "${Node_EXECUTABLE_DIR}/.."
        "${Node_EXECUTABLE_DIR}/../lib"
    )
endif()

find_path(NodeAddonApi_INCLUDE_DIR napi.h
    PATHS
        /usr/share/nodejs/node-addon-api
)

find_path(NodeApi_INCLUDE_DIR node_api.h
    PATHS
        ${NodeApi_INCLUDE_HINTS}
        /usr/include/node
        /usr/include/nodejs/src
        /usr/local/include/node
        /opt/homebrew/include/node
)
if(WIN32)
    find_library(Node_LIBRARY NAMES node libnode
        PATHS ${NodeApi_LIBRARY_HINTS}
    )
endif()

set(DYNABRIDGE_CAN_BUILD_NAPI_RUNTIME OFF)
if(NodeApi_INCLUDE_DIR AND Node_EXECUTABLE)
    if(NOT WIN32 OR Node_LIBRARY)
        set(DYNABRIDGE_CAN_BUILD_NAPI_RUNTIME ON)
    endif()
endif()

if(NodeApi_INCLUDE_DIR)
    add_executable(napi_backend_real_header_test
        tests/napi_backend_real_header_test.cpp
    )
    target_include_directories(napi_backend_real_header_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${NodeApi_INCLUDE_DIR}"
    )
    add_test(NAME napi_backend_real_header COMMAND napi_backend_real_header_test)
endif()

if(DYNABRIDGE_CAN_BUILD_NAPI_RUNTIME)
    add_library(napi_runtime_addon MODULE
        tests/napi_runtime_addon.cpp
    )
    set_target_properties(napi_runtime_addon PROPERTIES
        PREFIX ""
        SUFFIX ".node"
    )
    target_include_directories(napi_runtime_addon PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${NodeApi_INCLUDE_DIR}"
    )
    if(APPLE)
        target_link_options(napi_runtime_addon PRIVATE
            "-undefined"
            "dynamic_lookup"
        )
    elseif(WIN32 AND Node_LIBRARY)
        target_link_libraries(napi_runtime_addon PRIVATE "${Node_LIBRARY}")
    endif()
    add_test(
        NAME napi_runtime
        COMMAND "${Node_EXECUTABLE}"
            "--expose-gc"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests/napi_runtime_smoke.js"
            "$<TARGET_FILE:napi_runtime_addon>"
    )
endif()

if(DYNABRIDGE_BUILD_BENCHMARKS AND DYNABRIDGE_CAN_BUILD_NAPI_RUNTIME)
    add_library(node_call_benchmark_addon MODULE
        benchmarks/node_call_benchmark_addon.cpp
    )
    set_target_properties(node_call_benchmark_addon PROPERTIES
        PREFIX ""
        SUFFIX ".node"
    )
    target_include_directories(node_call_benchmark_addon PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}"
        "${NodeApi_INCLUDE_DIR}"
    )
    if(NodeAddonApi_INCLUDE_DIR)
        set_property(TARGET node_call_benchmark_addon PROPERTY
            CXX_STANDARD 17
        )
        target_compile_definitions(node_call_benchmark_addon PRIVATE
            DYNABRIDGE_HAS_NODE_ADDON_API=1
        )
        target_include_directories(node_call_benchmark_addon PRIVATE
            "${NodeAddonApi_INCLUDE_DIR}"
        )
    endif()
    if(APPLE)
        target_link_options(node_call_benchmark_addon PRIVATE
            "-undefined"
            "dynamic_lookup"
        )
    elseif(WIN32 AND Node_LIBRARY)
        target_link_libraries(node_call_benchmark_addon PRIVATE "${Node_LIBRARY}")
    endif()
endif()

