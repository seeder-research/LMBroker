add_library(api_lib
  alerter.cpp
  metrics.cpp
  rest_api.cpp
)
target_include_directories(api_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(api_lib PUBLIC
  common_lib pool_lib tracker_lib
  httplib nlohmann_json Threads::Threads)
