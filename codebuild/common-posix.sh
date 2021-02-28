#!/bin/bash

set -e

if test -f "/tmp/setup_proxy_test_env.sh"; then
    source /tmp/setup_proxy_test_env.sh
    env
    ls /tmp
fi

git submodule update --init
mkdir build
cd build

cmake -DBUILD_DEPS=ON $@ ../
make
ctest --output-on-failure

cd ..
