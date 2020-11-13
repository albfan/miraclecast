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

