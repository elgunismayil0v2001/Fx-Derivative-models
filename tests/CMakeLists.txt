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
                          unit/test_running_stats.cpp unit/test_mc_engine.cpp
                          unit/test_heston_qe.cpp unit/test_variance_reduction.cpp
                          unit/test_sha256.cpp unit/test_normal.cpp unit/test_delta_convention.cpp
                          unit/test_quotes.cpp unit/test_smile_slice.cpp unit/test_vol_surface.cpp
                          unit/test_arbitrage_checks.cpp unit/test_brent.cpp unit/test_implied_volatility.cpp
                          unit/test_least_squares.cpp unit/test_heston_smile.cpp
                          unit/test_heston_calibration.cpp unit/test_barrier_closed_form.cpp
                          unit/test_vanna_volga.cpp unit/test_barrier_mc.cpp)

fxval_add_test_executable(fxval_validation_tests validation/test_catalogue_consistency.cpp
                          validation/test_v01_put_call_parity.cpp validation/test_v02_greeks_fd.cpp
                          validation/test_gk_reference.cpp validation/test_v03_heston_vs_gk.cpp
                          validation/test_v04_heston_robustness.cpp
                          validation/test_heston_reference.cpp
                          validation/test_v05_heston_mc_vs_analytic.cpp
                          validation/test_v07_mc_reproducibility.cpp
                          validation/test_mc_path_reference.cpp
                          validation/test_mc_qe_path_reference.cpp
                          validation/test_v06_feller_study.cpp
                          validation/test_v08_variance_reduction.cpp
                          validation/test_v09_delta_roundtrip.cpp
                          validation/test_smile_reference.cpp
                          validation/test_v10_smile_arbitrage.cpp
                          validation/test_calibration_reference.cpp
                          validation/test_v11_heston_calibration.cpp
                          validation/test_barrier_reference.cpp
                          validation/test_v12_barrier_mc.cpp
                          validation/test_v13_in_out_parity.cpp
                          validation/test_v14_vanna_volga_pivots.cpp
                          validation/test_v15_model_comparison.cpp)
target_compile_definitions(
  fxval_validation_tests
  PRIVATE "FXVAL_TEST_CATALOGUE_PATH=\"${PROJECT_SOURCE_DIR}/docs/test_catalogue.md\""
          "FXVAL_GK_REFERENCE_PATH=\"${PROJECT_SOURCE_DIR}/tests/validation/data/gk_reference.csv\""
          "FXVAL_HESTON_REFERENCE_PATH=\"${PROJECT_SOURCE_DIR}/tests/validation/data/heston_reference.csv\""
          "FXVAL_MC_PATH_REFERENCE_PATH=\"${PROJECT_SOURCE_DIR}/tests/validation/data/mc_path_reference.csv\""
          "FXVAL_MC_QE_PATH_REFERENCE_PATH=\"${PROJECT_SOURCE_DIR}/tests/validation/data/mc_qe_path_reference.csv\""
          "FXVAL_SMILE_REFERENCE_PATH=\"${PROJECT_SOURCE_DIR}/tests/validation/data/smile_reference.csv\""
          "FXVAL_HESTON_IV_REFERENCE_PATH=\"${PROJECT_SOURCE_DIR}/tests/validation/data/heston_iv_reference.csv\""
          "FXVAL_CALIBRATION_REFERENCE_PATH=\"${PROJECT_SOURCE_DIR}/tests/validation/data/calibration_reference.csv\""
          "FXVAL_BARRIER_REFERENCE_PATH=\"${PROJECT_SOURCE_DIR}/tests/validation/data/barrier_reference.csv\""
          "FXVAL_SMILE_SCENARIOS_PATH=\"${PROJECT_SOURCE_DIR}/configs/scenarios/smile_scenarios.csv\"")
target_include_directories(fxval_unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_include_directories(fxval_validation_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# ---------------------------------------------------------------------------
# Command-line layer (apps/): unit tests, regression tests, allocation test, end-to-end test
# ---------------------------------------------------------------------------
if(FXVAL_BUILD_APPS)
  add_executable(
    fxval_app_tests app/test_price_config.cpp app/test_price_runner.cpp app/test_report.cpp
                    app/test_cli.cpp app/test_run_metadata.cpp regression/test_price_regression.cpp)
  target_link_libraries(fxval_app_tests PRIVATE fxval_app GTest::gtest_main nlohmann_json::nlohmann_json)
  target_include_directories(fxval_app_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  target_compile_definitions(
    fxval_app_tests
    PRIVATE "FXVAL_CONFIG_EXAMPLES_DIR=\"${PROJECT_SOURCE_DIR}/configs/examples\""
            "FXVAL_REGRESSION_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}/regression/expected\"")
  fxval_set_warnings(fxval_app_tests)
  gtest_discover_tests(fxval_app_tests DISCOVERY_TIMEOUT 60)

  # Replaces operator new for the whole process, so it is a program of its own.
  add_executable(fxval_alloc_tests app/test_allocation_free.cpp)
  target_link_libraries(fxval_alloc_tests PRIVATE fxval::fxval GTest::gtest_main)
  target_include_directories(fxval_alloc_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  fxval_set_warnings(fxval_alloc_tests)
  gtest_discover_tests(fxval_alloc_tests DISCOVERY_TIMEOUT 60)

  add_test(
    NAME cli_end_to_end
    COMMAND ${CMAKE_COMMAND} -DPRICE=$<TARGET_FILE:fxval-price>
            -DCONFIGS=${PROJECT_SOURCE_DIR}/configs/examples
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/cli_end_to_end -P
            ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_cli.cmake)
endif()

if(FXVAL_BUILD_EXAMPLES)
  add_test(
    NAME smile_example_end_to_end
    COMMAND ${CMAKE_COMMAND} -DEXE=$<TARGET_FILE:fxval-example-smile>
            -DSCENARIOS=${PROJECT_SOURCE_DIR}/configs/scenarios/smile_scenarios.csv
            -DWORK=${CMAKE_CURRENT_BINARY_DIR}/smile_example -P
            ${CMAKE_CURRENT_SOURCE_DIR}/integration/run_smile_example.cmake)
endif()
