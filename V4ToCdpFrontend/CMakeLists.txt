# Builds the V4toCdpFrontend shared library with Qt6

cmake_minimum_required(VERSION 3.21)
project(V4toCdpFrontend LANGUAGES CXX)

# Set C++ standard and enable Qt MOC
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Qml WebSockets HttpServer)

set(SOURCES
    CdpDebuggerFrontend.cpp
    V4CdpMapper.cpp
    V4Helpers.cpp
)

set(HEADERS
    V4CdpMapper.h
    CdpDebuggerFrontend.h
    V4Helpers.h
)

add_library(V4toCdpFrontend SHARED ${SOURCES} ${HEADERS})
add_library(V4::CdpFrontend ALIAS V4toCdpFrontend)

target_include_directories(V4toCdpFrontend PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(V4toCdpFrontend PRIVATE
    V4TOCDPFRONTEND_LIB
)

target_compile_options(V4toCdpFrontend PRIVATE
    -Wall -Wextra
    -std=c++17
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3>
    -fexceptions
)

target_link_libraries(V4toCdpFrontend PRIVATE
    Qt6::Core
    Qt6::WebSockets
    Qt6::HttpServer
    Qt6::Qml
)

install(TARGETS V4toCdpFrontend
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
