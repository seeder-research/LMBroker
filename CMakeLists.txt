cmake_minimum_required(VERSION 3.16)
project(flexlm_broker VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BUILD_TESTS  "Build unit and integration tests" ON)
option(ENABLE_ASAN  "Enable AddressSanitizer"          OFF)

if(ENABLE_ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

find_package(Threads REQUIRED)
find_package(OpenSSL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQ REQUIRED libpq)

# ── nlohmann/json ─────────────────────────────────────────────────────────────
# Prefer system package (apt: nlohmann-json3-dev), fall back to vendored
find_path(NLOHMANN_INCLUDE_DIR nlohmann/json.hpp
  PATHS /usr/include /usr/local/include
        ${CMAKE_SOURCE_DIR}/third_party)
if(NOT NLOHMANN_INCLUDE_DIR)
  message(FATAL_ERROR "nlohmann/json not found. Run: apt install nlohmann-json3-dev  (or place json.hpp at third_party/nlohmann/json.hpp)")
endif()
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE ${NLOHMANN_INCLUDE_DIR})

# ── cpp-httplib ───────────────────────────────────────────────────────────────
# Prefer system compiled package (apt: libcpp-httplib-dev).
# Falls back to vendored header-only if pkg-config not found.
pkg_check_modules(HTTPLIB QUIET cpp-httplib)
if(HTTPLIB_FOUND)
  add_library(httplib INTERFACE)
  target_include_directories(httplib INTERFACE ${HTTPLIB_INCLUDE_DIRS})
  target_link_libraries(httplib INTERFACE ${HTTPLIB_LIBRARIES})
  target_link_directories(httplib INTERFACE ${HTTPLIB_LIBRARY_DIRS})
  message(STATUS "httplib: system (${HTTPLIB_VERSION})")
else()
  find_path(HTTPLIB_INCLUDE_DIR httplib.h
    PATHS /usr/include /usr/local/include
          ${CMAKE_SOURCE_DIR}/third_party/httplib)
  if(NOT HTTPLIB_INCLUDE_DIR)
    message(FATAL_ERROR "httplib not found. Run: apt install libcpp-httplib-dev")
  endif()
  add_library(httplib INTERFACE)
  target_include_directories(httplib INTERFACE ${HTTPLIB_INCLUDE_DIR})
  message(STATUS "httplib: header-only (vendored)")
endif()

# ── spdlog ────────────────────────────────────────────────────────────────────
# Prefer system package (apt: libspdlog-dev), fall back to vendored
find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
  find_path(SPDLOG_INCLUDE_DIR spdlog/spdlog.h
    PATHS /usr/include /usr/local/include
          ${CMAKE_SOURCE_DIR}/third_party/spdlog/include)
  if(NOT SPDLOG_INCLUDE_DIR)
    message(FATAL_ERROR "spdlog not found. Run: apt install libspdlog-dev")
  endif()
  add_library(spdlog INTERFACE)
  target_include_directories(spdlog INTERFACE ${SPDLOG_INCLUDE_DIR})
else()
  # spdlog found via find_package — alias to our expected target name
  add_library(spdlog INTERFACE)
  target_link_libraries(spdlog INTERFACE spdlog::spdlog)
endif()

# ── GTest ─────────────────────────────────────────────────────────────────────
if(BUILD_TESTS)
  find_package(GTest QUIET)
  if(NOT GTest_FOUND)
    # Try locating manually (built from /usr/src/googletest)
    find_library(GTEST_LIB gtest PATHS /usr/local/lib /usr/lib)
    find_library(GTEST_MAIN_LIB gtest_main PATHS /usr/local/lib /usr/lib)
    find_path(GTEST_INCLUDE_DIR gtest/gtest.h
      PATHS /usr/local/include /usr/include)
    if(GTEST_LIB AND GTEST_MAIN_LIB AND GTEST_INCLUDE_DIR)
      add_library(GTest::gtest      STATIC IMPORTED)
      add_library(GTest::gtest_main STATIC IMPORTED)
      set_target_properties(GTest::gtest      PROPERTIES IMPORTED_LOCATION ${GTEST_LIB}      INTERFACE_INCLUDE_DIRECTORIES ${GTEST_INCLUDE_DIR})
      set_target_properties(GTest::gtest_main PROPERTIES IMPORTED_LOCATION ${GTEST_MAIN_LIB} INTERFACE_INCLUDE_DIRECTORIES ${GTEST_INCLUDE_DIR})
      message(STATUS "GTest found manually: ${GTEST_LIB}")
    else()
      message(WARNING "GTest not found — tests will be skipped")
      set(BUILD_TESTS OFF)
    endif()
  endif()
endif()

include_directories(include)

add_subdirectory(src/common)
add_subdirectory(src/pool)
add_subdirectory(src/health)
add_subdirectory(src/tracker)
add_subdirectory(src/api)
add_subdirectory(src/broker)

add_executable(flexlm-broker src/main.cpp)
target_link_libraries(flexlm-broker PRIVATE
  broker_lib pool_lib health_lib tracker_lib api_lib common_lib
  Threads::Threads ${LIBPQ_LIBRARIES}
)
target_include_directories(flexlm-broker PRIVATE ${LIBPQ_INCLUDE_DIRS})

if(BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

install(TARGETS flexlm-broker DESTINATION bin)
install(FILES config/broker.conf.example DESTINATION etc/flexlm-broker)
