FROM docker.io/albfan/miraclecast-ci

COPY . ./

RUN rm -rf build-cmake; \
    mkdir build-cmake; \
    cd build-cmake; \
    cmake ..; \
    make
