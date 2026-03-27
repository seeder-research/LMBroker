add_executable(integration_tests
  test_db_connection.cpp
  test_pool_api.cpp
  test_tracker.cpp
)
target_link_libraries(integration_tests PRIVATE
  GTest::gtest_main tracker_lib pool_lib api_lib common_lib)
include(GoogleTest)
gtest_discover_tests(integration_tests PROPERTIES LABELS "integration")
