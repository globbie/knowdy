FROM globbie/build

RUN apt-get update && apt-get -y install \
        lcov \
        libreadline-dev

ADD . /src

RUN cd /src && rm -rf build && mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && make && make check-knowdy && make knowdy_coverage
