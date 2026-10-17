# Benchmarks are measurement tools, not tests: they are off by default and not run by ctest.
add_executable(fxval_bench_mc bench_mc_scaling.cpp)
target_link_libraries(fxval_bench_mc PRIVATE fxval::fxval)
fxval_set_warnings(fxval_bench_mc)
