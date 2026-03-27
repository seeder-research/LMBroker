find_package(GTest QUIET)
if(NOT GTest_FOUND)
  message(STATUS "GTest not found — skipping tests. Install libgtest-dev to enable.")
  return()
endif()

add_subdirectory(unit)
add_subdirectory(integration)
