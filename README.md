# Knowdy

[![Build Status](https://travis-ci.org/globbie/knowdy.svg?branch=master)](https://travis-ci.org/globbie/knowdy)

Meet Knowdy: a scalable knowledge base

## Build

* Compile sources


    mkdir build && cd build
    cmake .. && make

* Build package


    make package


After this actions you will get rpm package at your build directory. If you want to deploy your package see 
[infrastructure](https://github.com/globbie/infrastructure) repository.


## Install

### Installing from sources

* Run `make install` in you build directory.

### Installing on CentOS 7

* Add yum repository `http://repo.globbie.com/centos/$releasever/$basearch/`
* Install knowdy `yum install knowdy`


## Run

    systemctl start knd-collection
    systemctl start knd-delivery
    systemctl start knd-retriever
    systemctl start knd-learner
    systemctl start knd-storage
