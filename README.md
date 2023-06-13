# Knowdy

[![Build Status](https://github.com/globbie/knowdy/workflows/Knowdy%20CI/badge.svg)](https://github.com/globbie/knowdy/actions?query=workflow%3A%22Knowdy+CI%22)

[![Travis Status](https://travis-ci.org/globbie/knowdy.svg?branch=master)](https://travis-ci.org/globbie/knowdy)


Conceptual graph database.

## Build

### Dependencies list

* cmake
* gcc
* make
* pkg-config
* libevent-dev
* check
* libsubunit-dev
* valgrind

### Before compilation

```bash
git submodule update --init --recursive
```

### Compilation

```bash
mkdir build && cd build
cmake .. && make
```

### Test

Execute `make check-knowdy` in your build directory to run tests.

## Install

Run `make install` in your build directory.

