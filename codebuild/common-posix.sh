#!/bin/bash

set -e

git submodule update --init
mkdir build
cd build

cmake -DBUILD_DEPS=ON $@ ../
make
ctest --output-on-failure

cd ..
