# Builds the V4ScriptDebugger shared library with Qt6

cmake_minimum_required(VERSION 3.21)
project(V4ScriptDebugger LANGUAGES CXX)

# Set C++ standard and enable Qt MOC
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Qml)

set(SOURCES
    V4ScriptDebuggerBackend.cpp
    V4DebugAgent.cpp
    V4DebugHandler.cpp
    V4DebugJobs.cpp
    V4EngineExt.cpp
    V4ScriptDebuggerApi.cpp
)

set(HEADERS
    v4scriptdebugger_global.h
    V4DebugJobs.h
    V4ScriptDebuggerApi.h
    V4ScriptDebuggerBackend.h
    V4EngineExt.h
    V4DebugHandler.h
    V4DebugAgent.h
)

# Define shared library
add_library(V4ScriptDebugger SHARED ${SOURCES} ${HEADERS})
add_library(V4::V4ScriptDebuggerBackend ALIAS V4ScriptDebugger)

# Returns a list of Qt private include paths required for: CV4EngineExt and co
function(qt_private_include_paths OUT_VAR)
    # we use Qt6::Core to extract the Qt base include directory
    # INTERFACE_INCLUDE_DIRECTORIES contains:
    #   [0] .../include/QtCore
    #   [1] .../include          <-- this is the base we need
    get_target_property(_qt_include_dirs Qt6::Core INTERFACE_INCLUDE_DIRECTORIES)
    if(NOT _qt_include_dirs)
        message(FATAL_ERROR "Qt6::Core target not found – call find_package(Qt6 COMPONENTS Core) first.")
    endif()
    list(GET _qt_include_dirs 1 QT_BASE_INCLUDE_DIR)   # base = .../include

    set(paths
        "${QT_BASE_INCLUDE_DIR}/QtQml/${Qt6_VERSION}/QtQml"    # eg for: private/qv4jscall_p.h
        "${QT_BASE_INCLUDE_DIR}/QtQml/${Qt6_VERSION}"          # eg for: QtQml/private/qv4function_p.h
        "${QT_BASE_INCLUDE_DIR}/QtCore/${Qt6_VERSION}/QtCore"  # eg for: private/qobject_p.h
        "${QT_BASE_INCLUDE_DIR}/QtCore/${Qt6_VERSION}"         # eg for: QtCore/private/qglobal_p.h
    )
    # Remove duplicates (just in case)
    list(REMOVE_DUPLICATES paths)

    set(${OUT_VAR} ${paths} PARENT_SCOPE)
endfunction()

# Include directories
qt_private_include_paths(QT_PRIVATE_PATHS)
target_include_directories(V4ScriptDebugger PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${QT_PRIVATE_PATHS}
)

# set #define to deactive not needed code for CDP FRONTEND
target_compile_definitions(V4ScriptDebugger PRIVATE CDP_FRONTEND)

# Compiler definitions (from .vcxproj)
target_compile_definitions(V4ScriptDebugger PRIVATE
    V4SCRIPTDEBUGGER_LIB
)

# Compiler flags (from .vcxproj: Debug/Release)
target_compile_options(V4ScriptDebugger PRIVATE
    -Wall -Wextra
    -std=c++17
    $<$<CONFIG:Debug>:-g -O0>
    $<$<CONFIG:Release>:-O3>
    -fexceptions
)

target_link_libraries(V4ScriptDebugger PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
)

# Installation
install(TARGETS V4ScriptDebugger
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)
