cmake_minimum_required(VERSION 3.1)
project(aws-crt-cpp CXX)

if(POLICY CMP0028)
    cmake_policy(SET CMP0028 NEW)
endif()
if(POLICY CMP0048)
    cmake_policy(SET CMP0048 NEW)
endif()
if(POLICY CMP0054)
    cmake_policy(SET CMP0054 NEW)
endif()
if(POLICY CMP0056)
    cmake_policy(SET CMP0056 NEW)
endif()

option(BUILD_DEPS "Builds aws common runtime dependencies as part of build, only do this if you don't want to control your dependency chain." OFF)

if (BUILD_DEPS)
    set(AWS_DEPS_BUILD_DIR ${CMAKE_CURRENT_BINARY_DIR}/deps)
    if (DEFINED CMAKE_INSTALL_PREFIX)
        set(AWS_DEPS_INSTALL_DIR ${CMAKE_INSTALL_PREFIX})
    else()
        set(AWS_DEPS_INSTALL_DIR ${CMAKE_CURRENT_BINARY_DIR}/deps/install)
    endif()
    file(MAKE_DIRECTORY ${AWS_DEPS_BUILD_DIR})
    if(TARGET_ARCH STREQUAL "ANDROID")
        execute_process(
                COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR}
                -DTARGET_ARCH=${TARGET_ARCH}
                -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
                -DANDROID_NATIVE_API_LEVEL=${ANDROID_NATIVE_API_LEVEL}
                -DANDROID_ABI=${ANDROID_ABI}
                -DANDROID_TOOLCHAIN_NAME=${ANDROID_TOOLCHAIN_NAME}
                -DANDROID_STANDALONE_TOOLCHAIN=${ANDROID_STANDALONE_TOOLCHAIN}
                -DANDROID_STL=${ANDROID_STL}
                -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}
                -DCMAKE_INSTALL_PREFIX=${AWS_DEPS_INSTALL_DIR}
                -DGIT_EXECUTABLE=${GIT_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/aws-common-runtime
                WORKING_DIRECTORY ${AWS_DEPS_BUILD_DIR})
    else()
        execute_process(
                COMMAND ${CMAKE_COMMAND} -G ${CMAKE_GENERATOR}
                -DTARGET_ARCH=${TARGET_ARCH}
                -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
                -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
                -DBUILD_SHARED_LIBS=${BUILD_SHARED_LIBS}
                -DCMAKE_INSTALL_PREFIX=${AWS_DEPS_INSTALL_DIR}
                -DCMAKE_RUNTIME_OUTPUT_DIRECTORY=${CMAKE_CURRENT_BINARY_DIR}/bin
                ${CMAKE_CURRENT_SOURCE_DIR}/aws-common-runtime
                WORKING_DIRECTORY ${AWS_DEPS_BUILD_DIR}
                RESULT_VARIABLE BUILD_AWSCRT_EXIT_CODE)
    endif()

    if (NOT ${BUILD_AWSCRT_EXIT_CODE} EQUAL 0)
        message(FATAL_ERROR "Failed to configure aws crt libraries.")
    endif()
    execute_process(COMMAND ${CMAKE_COMMAND} --build ${AWS_DEPS_BUILD_DIR} --config ${CMAKE_BUILD_TYPE}
            RESULT_VARIABLE BUILD_AWSCRT_EXIT_CODE)

    if (NOT ${BUILD_AWSCRT_EXIT_CODE} EQUAL 0)
        message(FATAL_ERROR "Failed to build aws crt libraries.")
    endif()
    message(STATUS "AWS Common Runtime is installed at: ${AWS_DEPS_INSTALL_DIR}")
    list(APPEND CMAKE_PREFIX_PATH "${AWS_DEPS_INSTALL_DIR}")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_INSTALL_PREFIX}/lib/cmake")

if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 11)
endif()

file(GLOB AWS_CRT_HEADERS
        "include/aws/crt/*.h"
)

file(GLOB AWS_CRT_IO_HEADERS
        "include/aws/crt/io/*.h"
)

file(GLOB AWS_CRT_MQTT_HEADERS
        "include/aws/crt/mqtt/*.h"
)

file(GLOB AWS_CRT_EXTERNAL_HEADERS
        "include/aws/crt/external/*.h"
)

file(GLOB AWS_CRT_CPP_HEADERS
        ${AWS_CRT_HEADERS}
	    ${AWS_CRT_IO_HEADERS}
        ${AWS_CRT_MQTT_HEADERS}
)

file(GLOB AWS_CRT_SRC 
       "source/*.cpp"
)

file (GLOB AWS_CRT_IO_SRC
       "source/io/*.cpp"
)

file (GLOB AWS_CRT_MQTT_SRC
       "source/mqtt/*.cpp"
)

file(GLOB AWS_CRT_EXTERNAL_CRC
        "source/external/*.cpp"
)

file(GLOB AWS_CRT_CPP_SRC
        ${AWS_CRT_SRC}
        ${AWS_CRT_IO_SRC}
        ${AWS_CRT_MQTT_SRC}
        ${AWS_CRT_EXTERNAL_CRC}
)

if (WIN32)
    if (MSVC)
        source_group("Header Files\\aws\\crt" FILES ${AWS_CRT_HEADERS})
        source_group("Header Files\\aws\\crt\\io" FILES ${AWS_CRT_IO_HEADERS})
        source_group("Header Files\\aws\\crt\\mqtt" FILES ${AWS_CRT_MQTT_HEADERS})
        source_group("Header Files\\aws\\crt\\external" FILES ${AWS_CRT_EXTERNAL_HEADERS})

        source_group("Source Files" FILES ${AWS_CRT_SRC})
        source_group("Source Files\\io" FILES ${AWS_CRT_IO_SRC})
        source_group("Source Files\\mqtt" FILES ${AWS_CRT_MQTT_SRC})
        source_group("Source Files\\external" FILES ${AWS_CRT_EXTERNAL_SRC})
    endif ()
endif()

add_library(${CMAKE_PROJECT_NAME} ${AWS_CRT_CPP_SRC})

if (BUILD_SHARED_LIBS AND WIN32)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PUBLIC -DUSE_IMPORT_EXPORT)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE -DAWS_CRT_CPP_EXPORTS)
endif()

set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)

set(CMAKE_C_FLAGS_DEBUGOPT "")

#set warnings
if (MSVC)
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE /W4 /WX)
else ()
    target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE -Wall -Wno-long-long -pedantic -Werror)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "" OR CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE "-DDEBUG_BUILD")
endif ()

target_include_directories(${CMAKE_PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

find_package(aws-c-mqtt REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} AWS::aws-c-mqtt)

install(FILES ${AWS_CRT_HEADERS} DESTINATION "include/aws/crt")
install(FILES ${AWS_CRT_IO_HEADERS} DESTINATION "include/aws/crt/io")
install(FILES ${AWS_CRT_MQTT_HEADERS} DESTINATION "include/aws/crt/mqtt")
install(FILES ${AWS_CRT_EXTERNAL_HEADERS} DESTINATION "include/aws/crt/external")

install(
        TARGETS ${CMAKE_PROJECT_NAME}
        EXPORT ${CMAKE_PROJECT_NAME}-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib
)

install(EXPORT "${CMAKE_PROJECT_NAME}-targets"
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/${CMAKE_PROJECT_NAME}/cmake/"
        NAMESPACE AWS::)

configure_file("cmake/${CMAKE_PROJECT_NAME}-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}-config.cmake"
        @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_PROJECT_NAME}-config.cmake"
        DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/${CMAKE_PROJECT_NAME}/cmake/")

enable_testing()
add_subdirectory(tests)
add_subdirectory(samples/mqtt_pub_sub)
