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
set(AWS_C_COMMON_SHA "v0.3.0")
include(BuildAwsCCommon)

if (UNIX AND NOT APPLE)
    set(S2N_URL "https://github.com/awslabs/s2n.git")
    set(s2N_SHA "383586162b3ee60bbd75105fcfe583b14bf60d46")
    include(BuildS2N)
endif()

set(AWS_C_IO_URL "https://github.com/awslabs/aws-c-io.git")
set(AWS_C_IO_SHA "v0.2.0")
include(BuildAwsCIO)

set(AWS_C_MQTT_URL "https://github.com/awslabs/aws-c-mqtt.git")
set(AWS_C_MQTT_SHA "v0.2.0")
include(BuildAwsCMqtt)

add_dependencies(AwsCMqtt AwsCIO)

if (UNIX AND NOT APPLE)
    add_dependencies(AwsCIO S2N AwsCCommon)
else()
    add_dependencies(AwsCIO AwsCCommon )
endif()