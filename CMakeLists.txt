cmake_minimum_required(VERSION 3.21)
project(V4toCdpDevToolsProject LANGUAGES CXX)

option(BUILD_DEMO "Build the V4EngineExtDemo application" ON)
option(BUILD_LIBS "Build the libraries" ON)
option(BUILD_TEST_CLIENT "websocket CDP test client" ON)


add_subdirectory(common)

if(BUILD_LIBS)
    add_subdirectory(V4ScriptDebugger)
    add_subdirectory(V4ToCdpFrontend)
endif()

if(BUILD_DEMO)
    add_subdirectory(V4EngineExtDemo)
endif()

if(BUILD_TEST_CLIENT)
    add_subdirectory(CdpTestClient)
endif()
