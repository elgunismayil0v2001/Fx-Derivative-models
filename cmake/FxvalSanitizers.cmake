# Global sanitizer and coverage switches. They apply to the whole build tree, which is what
# sanitizers need (all code, including tests, must be instrumented consistently).
if(FXVAL_SANITIZE AND FXVAL_COVERAGE)
  message(FATAL_ERROR "Use either FXVAL_SANITIZE or FXVAL_COVERAGE, not both.")
endif()

if(FXVAL_SANITIZE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "Sanitizers need GCC or Clang.")
  endif()
  string(REPLACE ";" "," _fxval_san "${FXVAL_SANITIZE}")
  message(STATUS "fxval: sanitizers enabled: ${_fxval_san}")
  add_compile_options(-fsanitize=${_fxval_san} -fno-omit-frame-pointer
                      -fno-sanitize-recover=all)
  add_link_options(-fsanitize=${_fxval_san})
endif()

if(FXVAL_COVERAGE)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    message(FATAL_ERROR "Coverage needs GCC or Clang.")
  endif()
  message(STATUS "fxval: coverage instrumentation enabled")
  add_compile_options(--coverage -O0 -g)
  add_link_options(--coverage)
endif()
