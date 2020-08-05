# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0.
cmake_minimum_required (VERSION 3.1)
project (aws-c-event-stream C)

if (DEFINED CMAKE_PREFIX_PATH)
    file(TO_CMAKE_PATH "${CMAKE_PREFIX_PATH}" CMAKE_PREFIX_PATH)
endif()

if (DEFINED CMAKE_INSTALL_PREFIX)
    file(TO_CMAKE_PATH "${CMAKE_INSTALL_PREFIX}" CMAKE_INSTALL_PREFIX)
endif()

if (UNIX AND NOT APPLE)
    include(GNUInstallDirs)
elseif(NOT DEFINED CMAKE_INSTALL_LIBDIR)
    set(CMAKE_INSTALL_LIBDIR "lib")
endif()

# This is required in order to append /lib/cmake to each element in CMAKE_PREFIX_PATH
set(AWS_MODULE_DIR "/${CMAKE_INSTALL_LIBDIR}/cmake")
string(REPLACE ";" "${AWS_MODULE_DIR};" AWS_MODULE_PATH "${CMAKE_PREFIX_PATH}${AWS_MODULE_DIR}")
# Append that generated list to the module search path
list(APPEND CMAKE_MODULE_PATH ${AWS_MODULE_PATH})

include(AwsCFlags)
include(AwsSharedLibSetup)
include(AwsSanitizers)
include(CheckCCompilerFlag)
include(AwsFindPackage)

if(NOT MSVC)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
endif()

file(GLOB AWS_EVENT_STREAM_HEADERS
     "include/aws/event-stream/*.h"
)

file(GLOB AWS_EVENT_STREAM_PRIV_HEADERS
     "include/aws/event-stream/private/*.h"
)

file(GLOB AWS_EVENT_STREAM_SRC
    "source/*.c"
)

if(WIN32)
     if(MSVC)
         source_group("Header Files\\aws\\event-stream" FILES ${AWS_EVENT_STREAM_HEADERS})
         source_group("Source Files" FILES ${AWS_EVENT_STREAM_SRC})
     endif()
endif()

file(GLOB EVENT_STREAM_HEADERS
    ${AWS_EVENT_STREAM_HEADERS}
)

file(GLOB EVENT_STREAM_SRC
    ${AWS_EVENT_STREAM_SRC}
)

add_library(${PROJECT_NAME} ${EVENT_STREAM_SRC})
aws_set_common_properties(${PROJECT_NAME})
aws_add_sanitizers(${PROJECT_NAME})
aws_prepare_symbol_visibility_args(${PROJECT_NAME} "AWS_EVENT_STREAM")

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>)


set_target_properties(${PROJECT_NAME} PROPERTIES VERSION 1.0.0)
set_target_properties(${PROJECT_NAME} PROPERTIES SOVERSION 0unstable)

aws_use_package(aws-c-common)
aws_use_package(aws-checksums)

target_link_libraries(${PROJECT_NAME} PUBLIC ${DEP_AWS_LIBS})

aws_prepare_shared_lib_exports(${PROJECT_NAME})

install(FILES ${AWS_EVENT_STREAM_HEADERS}
    DESTINATION "include/aws/event-stream"
    COMPONENT Development)

if (BUILD_SHARED_LIBS)
   set (TARGET_DIR "shared")
else()
   set (TARGET_DIR "static")
endif()

install(EXPORT "${PROJECT_NAME}-targets"
    DESTINATION "${LIBRARY_DIRECTORY}/${PROJECT_NAME}/cmake/${TARGET_DIR}/"
    NAMESPACE AWS::
    COMPONENT Development)

configure_file("cmake/${PROJECT_NAME}-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
    @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
    DESTINATION "${LIBRARY_DIRECTORY}/${PROJECT_NAME}/cmake/"
    COMPONENT Development)


include(CTest)
enable_testing()
add_subdirectory(tests)

if(NOT MSVC)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
endif()

add_subdirectory(bin)
