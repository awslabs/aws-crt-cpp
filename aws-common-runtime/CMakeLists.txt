cmake_minimum_required(VERSION 3.1)

project(aws-crt)
list(APPEND CMAKE_MODULE_PATH "${PROJECT_SOURCE_DIR}/cmake")

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

include(ExternalProject)

set(AWS_DEPS_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}" CACHE PATH "Dependencies install directory.")
set(AWS_DEPS_BIN_DIR "${AWS_DEPS_INSTALL_DIR}/bin" CACHE PATH "Dependencies binary install directory.")
set(AWS_DEPS_LIB_DIR "${AWS_DEPS_INSTALL_DIR}/lib" CACHE PATH "Dependencies library install directory.")
set(AWS_DEPS_BUILD_DIR "${CMAKE_BINARY_DIR}/build" CACHE PATH "Dependencies build directory.")
set(AWS_DEPS_DOWNLOAD_DIR "${AWS_DEPS_BUILD_DIR}/downloads" CACHE PATH "Dependencies download directory.")

message("install dir ${AWS_DEPS_INSTALL_DIR}")
set(AWS_C_COMMON_URL "https://github.com/awslabs/aws-c-common.git")
set(AWS_C_COMMON_SHA "v0.3.4")
include(BuildAwsCCommon)

if (UNIX AND NOT APPLE)
    set(S2N_URL "https://github.com/awslabs/s2n.git")
    set(S2N_SHA "b6c3ca8321062d5ece229315ebe39fe8d05c82db")
    include(BuildS2N)
endif()

set(AWS_C_IO_URL "https://github.com/awslabs/aws-c-io.git")
set(AWS_C_IO_SHA "v0.2.5")
include(BuildAwsCIO)

set(AWS_C_HTTP_URL "https://github.com/awslabs/aws-c-http.git")
set(AWS_C_HTTP_SHA "v0.1.0")
include(BuildAwsCHttp)

set(AWS_C_MQTT_URL "https://github.com/awslabs/aws-c-mqtt.git")
set(AWS_C_MQTT_SHA "v0.3.3")
include(BuildAwsCMqtt)

set(AWS_C_CAL_URL "https://github.com/awslabs/aws-c-cal.git")
set(AWS_C_CAL_SHA "v0.1.4")
include(BuildAwsCCal)

add_dependencies(AwsCMqtt AwsCIO)
add_dependencies(AwsCHttp AwsCIO)
add_dependencies(AwsCCal AwsCCommon)

if (UNIX AND NOT APPLE)
    add_dependencies(AwsCIO S2N AwsCCommon)
else()
    add_dependencies(AwsCIO AwsCCommon )
endif()

