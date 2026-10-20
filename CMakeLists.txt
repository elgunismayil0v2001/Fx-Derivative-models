cmake_minimum_required(VERSION 3.25)

project(
  fxval
  VERSION 0.8.0
  DESCRIPTION "FX derivative pricing and risk models with an independent validation suite"
  LANGUAGES CXX)

if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not supported. Use: cmake --preset release")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # needed later for the pybind11 module

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE
      Release
      CACHE STRING "Build type" FORCE)
endif()

option(FXVAL_BUILD_TESTS "Build the unit and validation tests" ON)
option(FXVAL_BUILD_BENCH "Build the benchmark programs in bench/" OFF)
option(FXVAL_BUILD_EXAMPLES "Build the runnable examples in examples/ (not the consumer project)" ON)
option(FXVAL_BUILD_APPS "Build the command-line programs in apps/ (needs nlohmann_json)" ON)
option(FXVAL_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)
option(FXVAL_FETCH_DEPS "Download missing third-party dependencies with FetchContent" ON)
option(FXVAL_COVERAGE "Instrument the build for line coverage (GCC or Clang)" OFF)
set(FXVAL_SANITIZE
    ""
    CACHE STRING "Comma-separated sanitizers, e.g. address,undefined or thread")

list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")
include(FxvalWarnings)
include(FxvalSanitizers)

# Determinism rule (see docs/adr/0001-error-policy.md): results must not depend on
# unsafe floating-point optimisations.
string(CONCAT _fxval_all_flags "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_RELEASE} "
              "${CMAKE_CXX_FLAGS_DEBUG} ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
if(_fxval_all_flags MATCHES "-ffast-math|-Ofast")
  message(FATAL_ERROR "-ffast-math and -Ofast are not allowed: they break reproducibility.")
endif()

# ---------------------------------------------------------------------------
# Library
# ---------------------------------------------------------------------------
configure_file(include/fxval/version.hpp.in
               "${PROJECT_BINARY_DIR}/generated/include/fxval/version.hpp" @ONLY)

find_package(Threads REQUIRED)

add_library(fxval STATIC src/contracts.cpp src/error.cpp src/units.cpp src/daycount.cpp
                        src/garman_kohlhagen.cpp src/heston.cpp src/mc_engine.cpp
                        src/mc_models.cpp src/mc_vanilla.cpp src/mc_barrier.cpp src/vanna_volga.cpp src/barrier_comparison.cpp src/sha256.cpp src/normal.cpp
                        src/delta_convention.cpp src/quotes.cpp src/smile_slice.cpp src/vol_surface.cpp
                        src/arbitrage_checks.cpp src/barrier_closed_form.cpp src/implied_volatility.cpp src/least_squares.cpp
                        src/heston_smile.cpp src/heston_calibration.cpp)
add_library(fxval::fxval ALIAS fxval)

target_include_directories(
  fxval
  PUBLIC "$<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>"
         "$<BUILD_INTERFACE:${PROJECT_BINARY_DIR}/generated/include>"
         "$<INSTALL_INTERFACE:include>")
target_compile_features(fxval PUBLIC cxx_std_20)
target_link_libraries(fxval PUBLIC Threads::Threads)  # the Monte Carlo engine uses std::jthread
# Contract checks (FXVAL_EXPECTS) are active in Debug builds, including for consumers.
target_compile_definitions(fxval PUBLIC "$<$<CONFIG:Debug>:FXVAL_ENABLE_CONTRACTS>")
fxval_set_warnings(fxval)

include(GNUInstallDirs)

# ---------------------------------------------------------------------------
# Command-line programs (apps/): not part of the library's public API
# ---------------------------------------------------------------------------
if(FXVAL_BUILD_APPS)
  add_subdirectory(apps)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
if(FXVAL_BUILD_EXAMPLES)
  add_subdirectory(examples/smile)
endif()

if(FXVAL_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(FXVAL_BUILD_BENCH)
  add_subdirectory(bench)
endif()

# ---------------------------------------------------------------------------
# Install and package config: find_package(fxval) then link fxval::fxval
# ---------------------------------------------------------------------------
include(CMakePackageConfigHelpers)

install(
  TARGETS fxval
  EXPORT fxvalTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  INCLUDES
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(
  DIRECTORY include/fxval
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING
  PATTERN "*.hpp")
install(FILES "${PROJECT_BINARY_DIR}/generated/include/fxval/version.hpp"
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fxval)
install(
  EXPORT fxvalTargets
  NAMESPACE fxval::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fxval)

write_basic_package_version_file(
  "${PROJECT_BINARY_DIR}/fxvalConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion)
configure_package_config_file(
  cmake/fxvalConfig.cmake.in "${PROJECT_BINARY_DIR}/fxvalConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fxval)
install(FILES "${PROJECT_BINARY_DIR}/fxvalConfig.cmake"
              "${PROJECT_BINARY_DIR}/fxvalConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/fxval)
