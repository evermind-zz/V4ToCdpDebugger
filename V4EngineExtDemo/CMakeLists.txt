cmake_minimum_required(VERSION 3.21)
project(QJSEngineRunner)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Qml)

add_executable(jsrunner
    main.cpp
    DebuggerWorker.cpp
    EngineManager.cpp
)

# set #define to deactive not needed code for CDP FRONTEND
target_compile_definitions(jsrunner PRIVATE CDP_FRONTEND)

target_link_libraries(jsrunner PRIVATE
    Qt6::Core
    Qt6::Qml
    V4::V4ScriptDebuggerBackend
    V4::CdpFrontend
)
