# A tiny external project that uses an INSTALLED fxval. CI builds it to prove that
# find_package(fxval) and the fxval::fxval target work for a real consumer.
#
#   cmake --install build/release --prefix /tmp/fxval-install
#   cmake -S examples/consumer -B build/consumer -DCMAKE_PREFIX_PATH=/tmp/fxval-install
#   cmake --build build/consumer && ./build/consumer/consumer
cmake_minimum_required(VERSION 3.25)
project(fxval_consumer LANGUAGES CXX)

find_package(fxval 0.11 CONFIG REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE fxval::fxval)
