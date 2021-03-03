#!/bin/bash

set -e

if test -f "/tmp/setup_proxy_test_env.sh"; then
    source /tmp/setup_proxy_test_env.sh
    env
    ls /tmp
    curl --verbose --proxy http://ec2-100-25-139-228.compute-1.amazonaws.com:3128 www.example.com
fi

git submodule update --init
mkdir build
cd build

cmake -DBUILD_DEPS=ON $@ ../
make
ctest --output-on-failure

cd ..
