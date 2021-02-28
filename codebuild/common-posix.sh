#!/bin/bash

set -e

if test -f "/tmp/setup_proxy_test_env.sh"; then
    /tmp/setup_proxy_test_env.sh
    env
fi

git submodule update --init
mkdir build
cd build

cmake -DBUILD_DEPS=ON $@ ../
make
ctest --output-on-failure

cd ..
