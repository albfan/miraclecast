FROM docker.io/albfan/miraclecast-ci

COPY . ./

RUN rm -rf build-meson; \
    meson build-meson; \
    ninja -C build-meson
