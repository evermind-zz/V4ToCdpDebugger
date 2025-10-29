cmake_minimum_required(VERSION 3.21)
project(CDPTestClient LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network WebSockets)

add_executable(cdp_test_client
    cdp_test_client.cpp
    raw_settings.cpp
)

target_link_libraries(cdp_test_client
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
)
