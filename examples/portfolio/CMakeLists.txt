# Example application: value, Greeks, VaR and CVA of an FX book (docs/portfolio_mathematics.md).
# The analysis is a small static library so that the tests can link it; the program is main.cpp.
add_library(fxval_portfolio_core STATIC portfolio_analysis.cpp report_writers.cpp)
target_link_libraries(fxval_portfolio_core PUBLIC fxval::fxval)
target_include_directories(fxval_portfolio_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
fxval_set_warnings(fxval_portfolio_core)

add_executable(fxval-portfolio main.cpp)
target_link_libraries(fxval-portfolio PRIVATE fxval_portfolio_core)
fxval_set_warnings(fxval-portfolio)
