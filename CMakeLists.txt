cmake_minimum_required(VERSION 3.16)
project(em_filter_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

# nlohmann/json — header-only, fetched via git at configure time
include(FetchContent)
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)
# This provides the nlohmann_json::nlohmann_json target with correct include paths.

add_library(em_filter_cpp STATIC
    src/config.cpp
    src/websocket.cpp
    src/connection.cpp
    src/runner.cpp
)

target_include_directories(em_filter_cpp PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(em_filter_cpp PRIVATE src)

target_link_libraries(em_filter_cpp PUBLIC
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    nlohmann_json::nlohmann_json
)

add_executable(echo_filter examples/echo_filter.cpp)
target_link_libraries(echo_filter PRIVATE em_filter_cpp)
