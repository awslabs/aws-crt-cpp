#!/bin/bash

set -e

mkdir build
cd build

cmake -DBUILD_DEPS=ON $@ ../
make
ctest -V

cd ..
