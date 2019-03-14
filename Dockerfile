FROM debian

RUN apt-get update && apt-get -y install \
        cmake \
        gcc \
        make \
        pkg-config \
        libevent-dev \
        check \
        libreadline-dev\
        libsubunit-dev \
        valgrind

ADD . /src

RUN cd /src && rm -rf build && mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Release .. && make && make check-knowdy && make install
RUN rm -rf /src
