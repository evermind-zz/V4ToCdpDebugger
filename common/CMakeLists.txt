# provides debuging headers as INTERFACE

add_library(common_debug INTERFACE)
add_library(Common::Debug ALIAS common_debug)

target_include_directories(common_debug
    INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}
)

# Option for debug logging see common/*.h
option(ENABLE_DEBUG_LOGGING "Enable debug output to stdout/file" OFF)

if(ENABLE_DEBUG_LOGGING)
    message(STATUS "ENABLE_DEBUG_LOGGING is ON -> DEBUG_LOGGING_ENABLED defined")
    target_compile_definitions(common_debug
        INTERFACE DEBUG_LOGGING_ENABLED
    )
else()
    message(STATUS "ENABLE_DEBUG_LOGGING is OFF")
endif()
