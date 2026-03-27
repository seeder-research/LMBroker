add_library(broker_lib
  broker.cpp
  connection.cpp
  framing.cpp
  protocol.cpp
  thread_pool.cpp
  virtual_license.cpp
)
target_include_directories(broker_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(broker_lib PUBLIC
  common_lib pool_lib tracker_lib Threads::Threads)
