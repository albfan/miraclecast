FROM docker.io/albfan/miraclecast-ci

COPY . ./

RUN rm -rf build-autotools ; \
    mkdir build-autotools; \
    cd build-autotools; \
    ../autogen.sh; \
    ../configure; \
    make; \
    make check
