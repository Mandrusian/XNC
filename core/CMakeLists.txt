cmake_minimum_required(VERSION 3.20)
project(snc_core VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(snc_core STATIC
    src/url_resolver.cpp
    src/blocker.cpp
    src/storage.cpp
    src/tab_model.cpp
)

target_include_directories(snc_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

if(MSVC)
    target_compile_options(snc_core PRIVATE /W4)
    target_compile_definitions(snc_core PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(snc_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

include(CTest)
if(BUILD_TESTING)
    add_executable(snc_core_tests tests/test_snc_core.cpp)
    target_link_libraries(snc_core_tests PRIVATE snc_core)
    if(MSVC)
        target_compile_options(snc_core_tests PRIVATE /W4)
        target_compile_definitions(snc_core_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(snc_core_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME snc_core_tests COMMAND snc_core_tests)
endif()
