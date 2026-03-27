add_library(health_lib health_monitor.cpp)
target_include_directories(health_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(health_lib PUBLIC common_lib pool_lib Threads::Threads)
