# Knowdy

[![Build Status](https://travis-ci.org/globbie/knowdy.svg?branch=master)](https://travis-ci.org/globbie/knowdy)

An embedded semantic graph database.
If you want to test as a HTTP service, please see [Gnode](https://github.com/globbie/gnode) Golang wrapper.

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

