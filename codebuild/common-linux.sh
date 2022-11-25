#!/bin/bash

set -e

git submodule update --init
mkdir build
cd build

cmake $@ ../
make
ctest --output-on-failure

cd ..
