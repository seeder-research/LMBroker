add_executable(integration_tests
  test_db_connection.cpp
  test_pool_api.cpp
)
target_link_libraries(integration_tests PRIVATE
  GTest::gtest_main tracker_lib pool_lib common_lib)
include(GoogleTest)
# Tag as integration so ctest can filter: ctest -L integration
gtest_discover_tests(integration_tests PROPERTIES LABELS "integration")
