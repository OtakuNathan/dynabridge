cmake_minimum_required(VERSION 3.16)

project(portal_package_consumer LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(portal 0.1 CONFIG REQUIRED)

add_executable(portal_package_consumer main.cpp metadata.cpp)
target_link_libraries(portal_package_consumer PRIVATE portal::portal)
target_include_directories(portal_package_consumer PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}"
)
enable_testing()
add_test(NAME package_consumer COMMAND portal_package_consumer)
