# Reproducible build and test environment.
#   docker build -t fxval .
# The build stage compiles the library and RUNS THE TESTS. If a test fails, the image does not
# build. The runtime stage carries only the installed library (apps arrive in week 6).
#
# TODO when publishing: pin the base image by digest (ubuntu:24.04@sha256:...).

FROM ubuntu:24.04 AS build

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
       build-essential cmake ninja-build git ca-certificates \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src
COPY . .

RUN cmake --preset release \
    && cmake --build --preset release \
    && ctest --preset release \
    && cmake --install build/release --prefix /opt/fxval

FROM ubuntu:24.04 AS runtime
COPY --from=build /opt/fxval /opt/fxval
ENV CMAKE_PREFIX_PATH=/opt/fxval
