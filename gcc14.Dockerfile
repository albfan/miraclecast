FROM gcc:14

COPY . ./

RUN apt-get update
RUN apt-get install -y libudev-dev
RUN apt-get install -y libsystemd-dev

RUN rm -rf build-autotools
RUN mkdir build-autotools
RUN cd build-autotools
RUN ../autogen.sh
RUN ../configure
RUN make
RUN make check
