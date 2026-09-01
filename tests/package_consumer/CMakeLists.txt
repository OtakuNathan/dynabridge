cmake_minimum_required(VERSION 3.16)

project(dynabridge_package_consumer LANGUAGES CXX)

find_package(dynabridge 0.1 CONFIG REQUIRED)

add_executable(dynabridge_package_consumer main.cpp)
target_link_libraries(dynabridge_package_consumer PRIVATE dynabridge::dynabridge)
target_include_directories(dynabridge_package_consumer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
