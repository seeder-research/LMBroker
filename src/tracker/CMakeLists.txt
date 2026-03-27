add_library(tracker_lib usage_tracker.cpp db_connection.cpp)
target_include_directories(tracker_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(tracker_lib PUBLIC common_lib ${LIBPQ_LIBRARIES} Threads::Threads)
target_include_directories(tracker_lib PUBLIC ${LIBPQ_INCLUDE_DIRS})
