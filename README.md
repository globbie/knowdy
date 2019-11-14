# Knowdy

![Build Status](https://github.com/globbie/knowdy/workflows/.github/workflows/ccpp.yml/badge.svg)

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
git submodules update --init --recursive
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

