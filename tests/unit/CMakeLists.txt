add_executable(unit_tests
  test_lmutil_parser.cpp
  test_virtual_license.cpp
  test_config_parser.cpp
  test_config_diff.cpp
  test_config_reloader.cpp
  test_framing.cpp
  test_protocol.cpp
  test_thread_pool.cpp
  test_metrics.cpp
  test_alerter.cpp
)
target_link_libraries(unit_tests PRIVATE
  GTest::gtest_main pool_lib broker_lib api_lib common_lib)
include(GoogleTest)
gtest_discover_tests(unit_tests)
