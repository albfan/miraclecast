FROM debian:buster-slim

RUN dpkg --add-architecture i386

RUN apt-get update && apt-get install -y \
      build-essential \
      systemd \
      libglib2.0-dev \
      libreadline-dev \
      libudev-dev \
      libsystemd-dev \
      libusb-dev \
      automake \
      autoconf \
      libtool \
      cmake \
      meson

COPY . ./

RUN rm -rf build-autotools ; \
    mkdir build-autotools; \
    cd build-autotools; \
    ../autogen.sh; \
    ../configure; \
    make; \
    make check

RUN rm -rf build-cmake; \
    mkdir build-cmake; \
    cd build-cmake; \
    cmake ..; \
    make

RUN rm -rf build-meson; \
    meson build-meson; \
    ninja -C build-meson
