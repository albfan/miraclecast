FROM docker.io/albfan/miraclecast-ci

RUN mkdir src

COPY . ./src

WORKDIR src

RUN cmake -Bbuild . && make -C build
