add_library(pool_lib pool_manager.cpp lmutil_wrapper.cpp)
target_include_directories(pool_lib PUBLIC ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(pool_lib PUBLIC common_lib Threads::Threads)
