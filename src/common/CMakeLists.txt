add_library(common_lib
  config.cpp
  config_reloader.cpp
  logger.cpp
)
target_include_directories(common_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(common_lib PUBLIC spdlog nlohmann_json Threads::Threads)
