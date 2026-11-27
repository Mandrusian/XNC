cmake_minimum_required(VERSION 3.20)
project(snc_linux VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core ${CMAKE_CURRENT_BINARY_DIR}/core EXCLUDE_FROM_ALL)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GTK3 REQUIRED IMPORTED_TARGET gtk+-3.0)
pkg_check_modules(WEBKIT2GTK REQUIRED IMPORTED_TARGET webkit2gtk-4.1)

add_executable(snc src/main.cpp)

target_link_libraries(snc PRIVATE
    snc_core
    PkgConfig::GTK3
    PkgConfig::WEBKIT2GTK
)

target_compile_options(snc PRIVATE -Wall -Wextra -Wpedantic)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    target_link_options(snc PRIVATE -s)
endif()
