# Strict warnings for the project's own targets. Third-party code is never touched.
function(fxval_set_warnings target)
  if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(
      ${target}
      PRIVATE -Wall
              -Wextra
              -Wpedantic
              -Wconversion
              -Wsign-conversion
              -Wshadow
              -Wnon-virtual-dtor
              -Wold-style-cast
              -Wcast-align
              -Woverloaded-virtual
              -Wnull-dereference
              -Wdouble-promotion
              -Wformat=2)
    if(FXVAL_WARNINGS_AS_ERRORS)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()
