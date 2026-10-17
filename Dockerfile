# Reproducible build and test environment.
#   docker build -t fxval .
# The build stage compiles the library and RUNS THE TESTS. If a test fails, the image does not
# build. The runtime stage carries the installed library and the fxval-price program:
#   docker run --rm -v "$PWD/configs:/configs" fxval --config /configs/examples/gk_vanillas.json
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
ENV PATH=/opt/fxval/bin:$PATH
ENTRYPOINT ["fxval-price"]
CMD ["--help"]
