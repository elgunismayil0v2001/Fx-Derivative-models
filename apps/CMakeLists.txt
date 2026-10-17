# Command-line programs. They are NOT part of the library's public API: the library (fxval::fxval)
# stays free of third-party dependencies, and only the apps need a JSON parser.

# nlohmann/json: use an installed or vcpkg copy if there is one, else download a pinned release.
find_package(nlohmann_json 3.11 CONFIG QUIET GLOBAL)
if(NOT nlohmann_json_FOUND)
  if(NOT FXVAL_FETCH_DEPS)
    message(FATAL_ERROR
            "nlohmann_json not found. Install it (vcpkg: 'nlohmann-json'), enable FXVAL_FETCH_DEPS, "
            "or configure with -DFXVAL_BUILD_APPS=OFF.")
  endif()
  include(FetchContent)
  FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE
    SYSTEM)
  set(JSON_BuildTests OFF CACHE INTERNAL "")
  set(JSON_Install OFF CACHE INTERNAL "")
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# build_info.hpp (git commit, compiler, build type) is regenerated on every build.
set(FXVAL_BUILD_INFO_DIR "${PROJECT_BINARY_DIR}/generated/apps")
set(FXVAL_BUILD_INFO_HEADER "${FXVAL_BUILD_INFO_DIR}/build_info.hpp")
add_custom_target(
  fxval_build_info
  COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=${PROJECT_SOURCE_DIR} -DOUTPUT=${FXVAL_BUILD_INFO_HEADER}
          "-DCOMPILER=${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}"
          -DBUILD_TYPE=$<CONFIG> -P ${PROJECT_SOURCE_DIR}/cmake/GenBuildInfo.cmake
  BYPRODUCTS ${FXVAL_BUILD_INFO_HEADER}
  COMMENT "Recording git commit and compiler in build_info.hpp"
  VERBATIM)
file(MAKE_DIRECTORY "${FXVAL_BUILD_INFO_DIR}")

add_library(
  fxval_app STATIC lib/price_config.cpp lib/price_runner.cpp lib/report.cpp lib/run_metadata.cpp
                   lib/cli.cpp)
add_dependencies(fxval_app fxval_build_info)
target_include_directories(fxval_app PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/lib" "${FXVAL_BUILD_INFO_DIR}")
target_link_libraries(fxval_app PUBLIC fxval::fxval PRIVATE nlohmann_json::nlohmann_json)
fxval_set_warnings(fxval_app)

add_executable(fxval-price price/main.cpp)
target_link_libraries(fxval-price PRIVATE fxval_app)
fxval_set_warnings(fxval-price)

install(TARGETS fxval-price RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
