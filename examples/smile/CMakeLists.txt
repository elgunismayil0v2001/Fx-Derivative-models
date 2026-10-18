add_executable(fxval-example-smile smile_scenarios.cpp)
target_link_libraries(fxval-example-smile PRIVATE fxval::fxval)
fxval_set_warnings(fxval-example-smile)
