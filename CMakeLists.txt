cmake_minimum_required(VERSION 3.16)
project(em_filter_cpp CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

find_package(PkgConfig REQUIRED)
pkg_check_modules(sodium REQUIRED IMPORTED_TARGET libsodium)

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

# cpp-httplib — header-only HTTP server/client, used for the Model A
# (direct) transport: /agent/query, /pop/gossip, /health, and the gossip
# push loop's outbound POSTs.
set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    httplib
    GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
    GIT_TAG        v0.54.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(httplib)
# This provides the httplib::httplib target with CPPHTTPLIB_OPENSSL_SUPPORT wired in.

add_library(em_filter_cpp STATIC
    src/config.cpp
    src/websocket.cpp
    src/crypto.cpp
    src/identity.cpp
    src/connection.cpp
    src/server.cpp
    src/wsclient.cpp
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
    httplib::httplib
    PkgConfig::sodium
)

if(WIN32)
    target_link_libraries(em_filter_cpp PUBLIC ws2_32)
endif()

add_executable(echo_filter examples/echo_filter.cpp)
target_link_libraries(echo_filter PRIVATE em_filter_cpp)

# --- Tests -----------------------------------------------------------------

option(EM_FILTER_CPP_BUILD_TESTS "Build em_filter_cpp tests" ON)

if(EM_FILTER_CPP_BUILD_TESTS)
    enable_testing()

    FetchContent_Declare(
        Catch2
        GIT_REPOSITORY https://github.com/catchorg/Catch2.git
        GIT_TAG        v3.6.0
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(Catch2)

    add_executable(em_filter_cpp_tests
        tests/test_crypto_vectors.cpp
        tests/test_identity.cpp
        tests/test_server.cpp
        tests/test_wsclient.cpp
    )
    target_include_directories(em_filter_cpp_tests PRIVATE src)
    target_link_libraries(em_filter_cpp_tests PRIVATE
        em_filter_cpp
        Catch2::Catch2WithMain
    )
    target_compile_definitions(em_filter_cpp_tests PRIVATE
        FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/crypto_vectors.json"
    )

    list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
    include(CTest)
    include(Catch)
    catch_discover_tests(em_filter_cpp_tests)
endif()
