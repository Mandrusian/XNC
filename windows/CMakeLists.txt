cmake_minimum_required(VERSION 3.20)
project(xnc_windows VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../core ${CMAKE_CURRENT_BINARY_DIR}/core EXCLUDE_FROM_ALL)

set(WEBVIEW2_DIR "${CMAKE_CURRENT_SOURCE_DIR}/packages/Microsoft.Web.WebView2" CACHE PATH "Path to extracted Microsoft.Web.WebView2 NuGet package")

if(NOT EXISTS "${WEBVIEW2_DIR}/build/native/include/WebView2.h")
    message(FATAL_ERROR "WebView2.h not found under ${WEBVIEW2_DIR}/build/native/include. Pass -DWEBVIEW2_DIR=... or install Microsoft.Web.WebView2 via NuGet.")
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(WEBVIEW2_ARCH "x64")
else()
    set(WEBVIEW2_ARCH "x86")
endif()

add_executable(xnc WIN32 src/main.cpp)

target_include_directories(xnc PRIVATE
    "${WEBVIEW2_DIR}/build/native/include"
)

target_link_libraries(xnc PRIVATE
    xnc_core
    "${WEBVIEW2_DIR}/build/native/${WEBVIEW2_ARCH}/WebView2LoaderStatic.lib"
    user32
    gdi32
    comctl32
    dwmapi
    ole32
    shlwapi
    version
)

target_compile_definitions(xnc PRIVATE UNICODE _UNICODE NOMINMAX _CRT_SECURE_NO_WARNINGS)
if(MSVC)
    target_compile_options(xnc PRIVATE /W4 /EHsc)
    target_link_options(xnc PRIVATE "/MANIFESTUAC:level='asInvoker' uiAccess='false'")
endif()
