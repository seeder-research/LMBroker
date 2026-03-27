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
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQ REQUIRED libpq)

# Header-only vendored deps
add_library(nlohmann_json INTERFACE)
target_include_directories(nlohmann_json INTERFACE third_party/nlohmann)

add_library(httplib INTERFACE)
target_include_directories(httplib INTERFACE third_party/httplib)

add_library(spdlog INTERFACE)
target_include_directories(spdlog INTERFACE third_party/spdlog/include)

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
