FROM debian

RUN apt-get update && apt-get -y install \
        cmake \
        gcc \
        make \
        pkg-config \
        libevent-dev \
        check \
        libsubunit-dev

ADD . /src

RUN cd /src && rm -rf build && mkdir build && cd build && cmake .. && make && make install
RUN rm -rf /src
