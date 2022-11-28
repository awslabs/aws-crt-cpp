#!/bin/bash

set -e

if test -f "/tmp/setup_proxy_test_env.sh"; then
    source /tmp/setup_proxy_test_env.sh
fi


# Check if environment variable get set
echo $AWS_TEST_MQTT5_DIRECT_MQTT_PORT

git submodule update --init
mkdir build
cd build

cmake -DENABLE_PROXY_INTEGRATION_TESTS=ON $@ ../
make
ctest --output-on-failure

cd ..
