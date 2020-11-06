#!/bin/bash

pushd $(dirname $0)/..

git submodule update --init
mkdir -p aws-common-runtime/s2n/libcrypto-build
pushd aws-common-runtime/s2n/libcrypto-build
curl -LO https://www.openssl.org/source/openssl-1.1.1-latest.tar.gz
tar -xzvf openssl-1.1.1-latest.tar.gz
pushd `tar ztf openssl-1.1.1-latest.tar.gz | head -n1 | cut -f1 -d/`
./config -fPIC no-shared \
    no-md2 no-rc5 no-rfc3779 no-sctp no-ssl-trace no-zlib  \
    no-hw no-mdc2 no-seed no-idea enable-ec_nistp_64_gcc_128 no-camellia\
    no-bf no-ripemd no-dsa no-ssl2 no-ssl3 no-capieng \
    -DSSL_FORBID_ENULL -DOPENSSL_NO_DTLS1 -DOPENSSL_NO_HEARTBEATS \
    --prefix=$HOME/crt-canary
make -j
make install_sw
popd
popd


mkdir -p build
pushd build
cmake3 .. -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_DEPS=ON \
    -DCMAKE_INSTALL_PREFIX=$HOME/crt-canary \
    -DCMAKE_PREFIX_PATH=$HOME/crt-canary

cmake3 --build . --target install
popd

popd
