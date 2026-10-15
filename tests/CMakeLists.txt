include(GoogleTest)

# GoogleTest: use an installed or vcpkg copy if there is one, else download a pinned release.
find_package(GTest CONFIG QUIET)
if(NOT GTest_FOUND)
  if(NOT FXVAL_FETCH_DEPS)
    message(FATAL_ERROR "GoogleTest not found. Install it (vcpkg: 'gtest') or enable FXVAL_FETCH_DEPS.")
  endif()
  include(FetchContent)
  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.15.2
    GIT_SHALLOW TRUE
    SYSTEM)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
endif()

function(fxval_add_test_executable name)
  add_executable(${name} ${ARGN})
  target_link_libraries(${name} PRIVATE fxval::fxval GTest::gtest_main)
  fxval_set_warnings(${name})
  gtest_discover_tests(${name} DISCOVERY_TIMEOUT 60)
endfunction()

fxval_add_test_executable(fxval_unit_tests unit/test_error.cpp unit/test_units.cpp
                          unit/test_version.cpp unit/test_daycount.cpp
                          unit/test_garman_kohlhagen.cpp unit/test_garman_kohlhagen_errors.cpp unit/test_heston.cpp
                          unit/test_quadrature.cpp unit/test_philox.cpp
                          unit/test_running_stats.cpp unit/test_mc_engine.cpp)

fxval_add_test_executable(fxval_validation_tests validation/test_catalogue_consistency.cpp
                          validation/test_v01_put_call_parity.cpp validation/test_v02_greeks_fd.cpp
                          validation/test_gk_reference.cpp validation/test_v03_heston_vs_gk.cpp
                          validation/test_v04_heston_robustness.cpp
                          validation/test_heston_reference.cpp
                          validation/test_v05_heston_mc_vs_analytic.cpp
                          validation/test_v07_mc_reproducibility.cpp
                          validation/test_mc_path_reference.cpp)
target_compile_definitions(
  fxval_validation_tests
  PRIVATE "FXVAL_TEST_CATALOGUE_PATH=\"${PROJECT_SOURCE_DIR}/docs/test_catalogue.md\""
          "FXVAL_GK_REFERENCE_PATH=\"${PROJECT_SOURCE_DIR}/tests/validation/data/gk_reference.csv\""
          "FXVAL_HESTON_REFERENCE_PATH=\"${PROJECT_SOURCE_DIR}/tests/validation/data/heston_reference.csv\""
          "FXVAL_MC_PATH_REFERENCE_PATH=\"${PROJECT_SOURCE_DIR}/tests/validation/data/mc_path_reference.csv\"")
target_include_directories(fxval_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(fxval_validation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
