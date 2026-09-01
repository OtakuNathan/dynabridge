cmake_minimum_required(VERSION 3.16)

project(dynabridge VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_EXTENSIONS OFF)

include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

add_library(dynabridge INTERFACE)
add_library(dynabridge::dynabridge ALIAS dynabridge)
target_compile_features(dynabridge INTERFACE cxx_std_14)
target_include_directories(dynabridge INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(DYNABRIDGE_IS_TOP_LEVEL ON)
else()
    set(DYNABRIDGE_IS_TOP_LEVEL OFF)
endif()

option(DYNABRIDGE_BUILD_TESTS "Build dynabridge tests" ${DYNABRIDGE_IS_TOP_LEVEL})
option(DYNABRIDGE_BUILD_EXAMPLES "Build dynabridge runtime examples" OFF)
option(DYNABRIDGE_BUILD_BENCHMARKS "Build dynabridge benchmark executables" OFF)

if(DYNABRIDGE_BUILD_TESTS OR DYNABRIDGE_BUILD_EXAMPLES OR DYNABRIDGE_BUILD_BENCHMARKS)
    find_package(Python3 COMPONENTS Interpreter Development)

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
        PATHS /usr/share/nodejs/node-addon-api
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
endif()

set(DYNABRIDGE_CAN_BUILD_NAPI_RUNTIME OFF)
if(NodeApi_INCLUDE_DIR AND Node_EXECUTABLE)
    if(NOT WIN32 OR Node_LIBRARY)
        set(DYNABRIDGE_CAN_BUILD_NAPI_RUNTIME ON)
    endif()
endif()

function(dynabridge_configure_python_module target output_name)
    set_target_properties(${target} PROPERTIES
        PREFIX ""
        OUTPUT_NAME "${output_name}"
    )
    if(WIN32)
        set_target_properties(${target} PROPERTIES SUFFIX ".pyd")
    endif()
    target_link_libraries(${target} PRIVATE dynabridge::dynabridge)
    target_include_directories(${target} PRIVATE ${Python3_INCLUDE_DIRS})
    if(APPLE)
        target_link_options(${target} PRIVATE "-undefined" "dynamic_lookup")
    else()
        target_link_libraries(${target} PRIVATE Python3::Python)
    endif()
endfunction()

function(dynabridge_configure_napi_module target)
    set_target_properties(${target} PROPERTIES PREFIX "" SUFFIX ".node")
    target_link_libraries(${target} PRIVATE dynabridge::dynabridge)
    target_include_directories(${target} PRIVATE "${NodeApi_INCLUDE_DIR}")
    if(APPLE)
        target_link_options(${target} PRIVATE "-undefined" "dynamic_lookup")
    elseif(WIN32 AND Node_LIBRARY)
        target_link_libraries(${target} PRIVATE "${Node_LIBRARY}")
    endif()
endfunction()

if(DYNABRIDGE_BUILD_TESTS)
    enable_testing()

    add_executable(fake_backend_callable_group_test tests/fake_backend_callable_group.cpp)
    target_link_libraries(fake_backend_callable_group_test PRIVATE dynabridge::dynabridge)
    add_test(NAME fake_backend_callable_group COMMAND fake_backend_callable_group_test)

    add_executable(custom_def_config_test tests/custom_def_config_test.cpp)
    target_link_libraries(custom_def_config_test PRIVATE dynabridge::dynabridge)
    add_test(NAME custom_def_config COMMAND custom_def_config_test)

    add_executable(optional_test tests/optional_test.cpp)
    target_link_libraries(optional_test PRIVATE dynabridge::dynabridge)
    add_test(NAME optional COMMAND optional_test)

    add_executable(rpc_backend_test tests/rpc_backend_test.cpp)
    target_link_libraries(rpc_backend_test PRIVATE dynabridge::dynabridge)
    add_test(NAME rpc_backend COMMAND rpc_backend_test)

    if(Python3_Development_FOUND)
        add_executable(python_backend_test tests/python_backend_test.cpp)
        target_link_libraries(python_backend_test PRIVATE
            dynabridge::dynabridge
            Python3::Python
        )
        add_test(NAME python_backend COMMAND python_backend_test)

        add_library(python_runtime_addon MODULE tests/python_runtime_addon.cpp)
        dynabridge_configure_python_module(
            python_runtime_addon
            dynabridge_python_runtime_addon
        )
        add_test(
            NAME python_runtime
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/python_runtime_smoke.py"
                "$<TARGET_FILE:python_runtime_addon>"
        )
    endif()

    add_executable(napi_backend_test tests/napi_backend_test.cpp)
    target_link_libraries(napi_backend_test PRIVATE dynabridge::dynabridge)
    target_include_directories(napi_backend_test PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/tests/napi_stub"
    )
    add_test(NAME napi_backend COMMAND napi_backend_test)

    if(NodeApi_INCLUDE_DIR)
        add_executable(napi_backend_real_header_test tests/napi_backend_real_header_test.cpp)
        target_link_libraries(napi_backend_real_header_test PRIVATE dynabridge::dynabridge)
        target_include_directories(napi_backend_real_header_test PRIVATE
            "${NodeApi_INCLUDE_DIR}"
        )
        add_test(NAME napi_backend_real_header COMMAND napi_backend_real_header_test)
    endif()

    if(DYNABRIDGE_CAN_BUILD_NAPI_RUNTIME)
        add_library(napi_runtime_addon MODULE tests/napi_runtime_addon.cpp)
        dynabridge_configure_napi_module(napi_runtime_addon)
        add_test(
            NAME napi_runtime
            COMMAND "${Node_EXECUTABLE}"
                "--expose-gc"
                "${CMAKE_CURRENT_SOURCE_DIR}/tests/napi_runtime_smoke.js"
                "$<TARGET_FILE:napi_runtime_addon>"
        )
    endif()
endif()

if(DYNABRIDGE_BUILD_EXAMPLES)
    if(Python3_Development_FOUND)
        add_library(dynabridge_python_example MODULE examples/python/module.cpp)
        dynabridge_configure_python_module(
            dynabridge_python_example
            dynabridge_python_example
        )
        if(DYNABRIDGE_BUILD_TESTS)
            add_test(
                NAME python_example
                COMMAND "${Python3_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/examples/python/smoke.py"
                    "$<TARGET_FILE:dynabridge_python_example>"
            )
        endif()
    endif()

    if(DYNABRIDGE_CAN_BUILD_NAPI_RUNTIME)
        add_library(dynabridge_napi_example MODULE examples/napi/addon.cpp)
        dynabridge_configure_napi_module(dynabridge_napi_example)
        if(DYNABRIDGE_BUILD_TESTS)
            add_test(
                NAME napi_example
                COMMAND "${Node_EXECUTABLE}"
                    "${CMAKE_CURRENT_SOURCE_DIR}/examples/napi/smoke.js"
                    "$<TARGET_FILE:dynabridge_napi_example>"
            )
        endif()
    endif()
endif()

if(DYNABRIDGE_BUILD_BENCHMARKS)
    find_package(Threads REQUIRED)
    add_executable(rpc_call_benchmark benchmarks/rpc_call_benchmark.cpp)
    target_link_libraries(rpc_call_benchmark PRIVATE
        dynabridge::dynabridge
        Threads::Threads
    )

    find_path(FLUX_FOUNDRY_INCLUDE_DIR flow/flow.h
        HINTS "${CMAKE_CURRENT_SOURCE_DIR}/../flux_foundry"
    )
    if(FLUX_FOUNDRY_INCLUDE_DIR)
        target_compile_definitions(rpc_call_benchmark PRIVATE
            DYNABRIDGE_HAS_FLUX_FOUNDRY=1
        )
        target_include_directories(rpc_call_benchmark SYSTEM PRIVATE
            "${FLUX_FOUNDRY_INCLUDE_DIR}"
        )

        add_executable(flow_composition_benchmark benchmarks/flow_composition_benchmark.cpp)
        set_property(TARGET flow_composition_benchmark PROPERTY CXX_STANDARD 20)
        target_include_directories(flow_composition_benchmark SYSTEM PRIVATE
            "${FLUX_FOUNDRY_INCLUDE_DIR}"
        )

        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            find_package(PkgConfig QUIET)
            if(PkgConfig_FOUND)
                pkg_check_modules(GIO2 QUIET IMPORTED_TARGET gio-2.0)
            endif()
            if(GIO2_FOUND)
                target_compile_definitions(rpc_call_benchmark PRIVATE DYNABRIDGE_HAS_GLIB=1)
                target_link_libraries(rpc_call_benchmark PRIVATE PkgConfig::GIO2)
            endif()
        endif()
    endif()

    find_path(RPCLIB_INCLUDE_DIR rpc/client.h)
    find_library(RPCLIB_LIBRARY NAMES rpc rpclib)
    if(RPCLIB_INCLUDE_DIR AND RPCLIB_LIBRARY)
        target_compile_definitions(rpc_call_benchmark PRIVATE DYNABRIDGE_HAS_RPCLIB=1)
        target_include_directories(rpc_call_benchmark SYSTEM PRIVATE "${RPCLIB_INCLUDE_DIR}")
        target_link_libraries(rpc_call_benchmark PRIVATE "${RPCLIB_LIBRARY}")
    endif()

    if(Python3_Development_FOUND)
        add_executable(python_call_benchmark benchmarks/python_call_benchmark.cpp)
        target_link_libraries(python_call_benchmark PRIVATE
            dynabridge::dynabridge
            Python3::Python
        )

        find_package(pybind11 CONFIG QUIET)
        if(pybind11_FOUND)
            target_compile_definitions(python_call_benchmark PRIVATE DYNABRIDGE_HAS_PYBIND11=1)
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
            set_property(TARGET python_call_benchmark PROPERTY CXX_STANDARD 17)
            target_compile_definitions(python_call_benchmark PRIVATE DYNABRIDGE_HAS_NANOBIND=1)
            target_link_libraries(python_call_benchmark PRIVATE nanobind-static)
        endif()
    endif()

    if(DYNABRIDGE_CAN_BUILD_NAPI_RUNTIME)
        add_library(node_call_benchmark_addon MODULE benchmarks/node_call_benchmark_addon.cpp)
        dynabridge_configure_napi_module(node_call_benchmark_addon)
        if(NodeAddonApi_INCLUDE_DIR)
            set_property(TARGET node_call_benchmark_addon PROPERTY CXX_STANDARD 17)
            target_compile_definitions(node_call_benchmark_addon PRIVATE
                DYNABRIDGE_HAS_NODE_ADDON_API=1
            )
            target_include_directories(node_call_benchmark_addon PRIVATE
                "${NodeAddonApi_INCLUDE_DIR}"
            )
        endif()
    endif()
endif()

install(TARGETS dynabridge EXPORT dynabridgeTargets)
install(DIRECTORY dynabridge DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(EXPORT dynabridgeTargets
    FILE dynabridgeTargets.cmake
    NAMESPACE dynabridge::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/dynabridge"
)

configure_package_config_file(
    cmake/dynabridgeConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/dynabridgeConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/dynabridge"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/dynabridgeConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/dynabridgeConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/dynabridgeConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/dynabridge"
)
install(FILES README.md LICENSE DESTINATION "${CMAKE_INSTALL_DATADIR}/dynabridge")
