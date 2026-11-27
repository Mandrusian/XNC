cmake_minimum_required(VERSION 3.20)
project(xnc_core VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(xnc_core STATIC
    src/url_resolver.cpp
    src/blocker.cpp
    src/storage.cpp
    src/tab_model.cpp
)

target_include_directories(xnc_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(xnc_core PRIVATE /W4)
    target_compile_definitions(xnc_core PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(xnc_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(xnc_core_tests tests/test_xnc_core.cpp)
    target_link_libraries(xnc_core_tests PRIVATE xnc_core)
    if(MSVC)
        target_compile_options(xnc_core_tests PRIVATE /W4)
        target_compile_definitions(xnc_core_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(xnc_core_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME xnc_core_tests COMMAND xnc_core_tests)
endif()
