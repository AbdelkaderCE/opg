cmake_minimum_required(VERSION 3.20)
project(opg VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
FetchContent_Declare(
  CLI11
  GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
  GIT_TAG        v2.4.2
  GIT_SHALLOW    TRUE
)
FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.18.3
  GIT_SHALLOW    TRUE
)
# Header-only, HTTPS via OpenSSL, no optional compression libraries.
set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json CLI11 httplib)

find_package(ZLIB REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(Threads REQUIRED)

add_executable(opg
  src/main.cpp
  src/manifest.cpp
  src/cmd_init.cpp
  src/cmd_pack.cpp
  src/cmd_publish.cpp
  src/cmd_install.cpp
  src/cmd_resolve.cpp
  src/lockfile.cpp
  src/resolver.cpp
  src/pack.cpp
  src/archive.cpp
  src/tar_extract.cpp
  src/registry.cpp
  src/ignore.cpp
  src/sha256.cpp
)
target_include_directories(opg PRIVATE src)
target_compile_definitions(opg PRIVATE OPG_VERSION="${PROJECT_VERSION}")
target_link_libraries(opg PRIVATE
  nlohmann_json::nlohmann_json CLI11::CLI11 httplib::httplib
  ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto Threads::Threads)

if(MSVC)
  target_compile_options(opg PRIVATE /W4)
else()
  target_compile_options(opg PRIVATE -Wall -Wextra -Wpedantic)
endif()

install(TARGETS opg RUNTIME DESTINATION bin)
