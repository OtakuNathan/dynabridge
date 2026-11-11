cmake_minimum_required(VERSION 3.16)

project(portal_package_consumer LANGUAGES CXX)

find_package(portal 0.1 CONFIG REQUIRED)

add_executable(portal_package_consumer main.cpp)
target_link_libraries(portal_package_consumer PRIVATE portal::portal)
target_include_directories(portal_package_consumer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
