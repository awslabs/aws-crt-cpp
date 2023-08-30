#!/bin/bash

set -e

if test -f "/tmp/setup_proxy_test_env.sh"; then
    source /tmp/setup_proxy_test_env.sh
fi

git submodule update --init
mkdir build
cd build

cmake -DENABLE_PROXY_INTEGRATION_TESTS=ON $@ ../
make
ctest --debug #--output-on-failure

cd ..
