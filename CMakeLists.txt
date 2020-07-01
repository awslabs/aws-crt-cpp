cmake_minimum_required(VERSION 3.1)
project(aws-crt-cpp CXX C)
option(BUILD_DEPS "Builds aws common runtime dependencies as part of build, only do this if you don't want to control your dependency chain." OFF)

include(CTest)

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

if (${CMAKE_INSTALL_LIBDIR} STREQUAL "lib64")
    set(FIND_LIBRARY_USE_LIB64_PATHS true)
endif()

if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 11)
endif()

if (NOT CMAKE_BUILD_TYPE)
    # setting this breaks C++ builds in visualc++, so don't do it.
    if (NOT WIN32)
        set(CMAKE_BUILD_TYPE "RelWithDebInfo")
    endif()
endif()

if (NOT DEFINED AWS_CRT_CPP_VERSION)
    set(AWS_CRT_CPP_VERSION "v0.8.6")
    # Try to determine the current git tag and use that as the version
    execute_process(COMMAND git describe --tags
        RESULT_VARIABLE GIT_EXIT_CODE
        OUTPUT_VARIABLE GIT_TAG
        ERROR_QUIET
        OUTPUT_STRIP_TRAILING_WHITESPACE)
    if (NOT GIT_EXIT_CODE)
        set(AWS_CRT_CPP_VERSION ${GIT_TAG})
    endif()
endif()

message(STATUS "AWS CRT C++ ${AWS_CRT_CPP_VERSION}")

configure_file(include/aws/crt/Config.h.in ${CMAKE_CURRENT_LIST_DIR}/include/aws/crt/Config.h @ONLY)

# This is required in order to append /lib/cmake to each element in CMAKE_PREFIX_PATH
set(AWS_MODULE_DIR "/${CMAKE_INSTALL_LIBDIR}/cmake")
string(REPLACE ";" "${AWS_MODULE_DIR};" AWS_MODULE_PATH "${CMAKE_PREFIX_PATH}${AWS_MODULE_DIR}")
# Append that generated list to the module search path
list(APPEND CMAKE_MODULE_PATH ${AWS_MODULE_PATH})

if (BUILD_DEPS)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/aws-common-runtime/aws-c-common/cmake")

    include(AwsFindPackage)

    set(IN_SOURCE_BUILD ON)
    set(BUILD_TESTING_PREV ${BUILD_TESTING})
    set(BUILD_TESTING OFF)
    add_subdirectory(aws-common-runtime/aws-c-common)

    if (UNIX AND NOT APPLE)
        add_subdirectory(aws-common-runtime/s2n)
    endif()

    add_subdirectory(aws-common-runtime/aws-c-io)
    add_subdirectory(aws-common-runtime/aws-c-cal)
    add_subdirectory(aws-common-runtime/aws-c-compression)
    add_subdirectory(aws-common-runtime/aws-c-http)
    add_subdirectory(aws-common-runtime/aws-c-auth)
    add_subdirectory(aws-common-runtime/aws-c-mqtt)
    set(BUILD_TESTING ${BUILD_TESTING_PREV})
else()
    include(AwsFindPackage)
    set(IN_SOURCE_BUILD OFF)
endif()

include(AwsCFlags)
include(AwsSharedLibSetup)

file(GLOB AWS_CRT_HEADERS
        "include/aws/crt/*.h"
)

file(GLOB AWS_CRT_AUTH_HEADERS
        "include/aws/crt/auth/*.h"
)

file(GLOB AWS_CRT_CRYPTO_HEADERS
        "include/aws/crt/crypto/*.h"
)

file(GLOB AWS_CRT_IO_HEADERS
        "include/aws/crt/io/*.h"
)

file(GLOB AWS_CRT_IOT_HEADERS
        "include/aws/iot/*.h"
)

file(GLOB AWS_CRT_MQTT_HEADERS
        "include/aws/crt/mqtt/*.h"
)

file(GLOB AWS_CRT_HTTP_HEADERS
        "include/aws/crt/http/*.h"
)

file(GLOB AWS_CRT_EXTERNAL_HEADERS
        "include/aws/crt/external/*.h"
)

file(GLOB AWS_CRT_CPP_HEADERS
        ${AWS_CRT_HEADERS}
        ${AWS_CRT_AUTH_HEADERS}
        ${AWS_CRT_CRYPTO_HEADERS}
        ${AWS_CRT_IO_HEADERS}
        ${AWS_CRT_IOT_HEADERS}
        ${AWS_CRT_MQTT_HEADERS}
        ${AWS_CRT_HTTP_HEADERS}
        ${AWS_CRT_EXTERNAL_HEADERS}
)

file(GLOB AWS_CRT_SRC
       "source/*.cpp"
)

file(GLOB AWS_CRT_AUTH_SRC
        "source/auth/*.cpp"
)

file(GLOB AWS_CRT_CRYPTO_SRC
       "source/crypto/*.cpp"
)

file (GLOB AWS_CRT_IO_SRC
       "source/io/*.cpp"
)

file (GLOB AWS_CRT_IOT_SRC
        "source/iot/*.cpp"
)

file (GLOB AWS_CRT_MQTT_SRC
       "source/mqtt/*.cpp"
)

file (GLOB AWS_CRT_HTTP_SRC
        "source/http/*.cpp"
)

file(GLOB AWS_CRT_EXTERNAL_CRC
        "source/external/*.cpp"
)

file(GLOB AWS_CRT_CPP_SRC
        ${AWS_CRT_SRC}
        ${AWS_CRT_AUTH_SRC}
        ${AWS_CRT_CRYPTO_SRC}
        ${AWS_CRT_IO_SRC}
        ${AWS_CRT_IOT_SRC}
        ${AWS_CRT_MQTT_SRC}
        ${AWS_CRT_HTTP_SRC}
        ${AWS_CRT_EXTERNAL_CRC}
)

if (WIN32)
    if (MSVC)
        source_group("Header Files\\aws\\crt" FILES ${AWS_CRT_HEADERS})
        source_group("Header Files\\aws\\crt\\auth" FILES ${AWS_CRT_AUTH_HEADERS})
        source_group("Header Files\\aws\\crt\\crypto" FILES ${AWS_CRT_CRYPTO_HEADERS})
        source_group("Header Files\\aws\\crt\\io" FILES ${AWS_CRT_IO_HEADERS})
        source_group("Header Files\\aws\\iot" FILES ${AWS_CRT_IOT_HEADERS})
        source_group("Header Files\\aws\\crt\\mqtt" FILES ${AWS_CRT_MQTT_HEADERS})
        source_group("Header Files\\aws\\crt\\http" FILES ${AWS_CRT_HTTP_HEADERS})
        source_group("Header Files\\aws\\crt\\external" FILES ${AWS_CRT_EXTERNAL_HEADERS})

        source_group("Source Files" FILES ${AWS_CRT_SRC})
        source_group("Source Files\\auth" FILES ${AWS_CRT_AUTH_SRC})
        source_group("Source Files\\crypto" FILES ${AWS_CRT_CRYPTO_SRC})
        source_group("Source Files\\io" FILES ${AWS_CRT_IO_SRC})
        source_group("Source Files\\iot" FILES ${AWS_CRT_IOT_SRC})
        source_group("Source Files\\mqtt" FILES ${AWS_CRT_MQTT_SRC})
        source_group("Source Files\\http" FILES ${AWS_CRT_HTTP_SRC})
        source_group("Source Files\\external" FILES ${AWS_CRT_EXTERNAL_SRC})
    endif ()
endif()

add_library(${PROJECT_NAME} ${AWS_CRT_CPP_SRC})

target_compile_definitions(${PROJECT_NAME} PRIVATE -DCJSON_HIDE_SYMBOLS)

if (BUILD_SHARED_LIBS)
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DAWS_CRT_CPP_USE_IMPORT_EXPORT)
    target_compile_definitions(${PROJECT_NAME} PRIVATE -DAWS_CRT_CPP_EXPORTS)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD ${CMAKE_CXX_STANDARD})

aws_prepare_symbol_visibility_args(${PROJECT_NAME} "AWS_CRT_CPP")

#set warnings and runtime library
if (MSVC)
    if(STATIC_CRT)
        target_compile_options(${PROJECT_NAME} PRIVATE "/MT$<$<CONFIG:Debug>:d>")
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE "/MD$<$<CONFIG:Debug>:d>")
    endif()
    target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX /wd4068)
else ()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wno-long-long -pedantic -Werror)
endif ()

if (CMAKE_BUILD_TYPE STREQUAL "" OR CMAKE_BUILD_TYPE MATCHES Debug)
    target_compile_definitions(${PROJECT_NAME} PRIVATE "-DDEBUG_BUILD")
endif ()

target_include_directories(${PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

aws_use_package(aws-c-http)
aws_use_package(aws-c-mqtt)
aws_use_package(aws-c-cal)
aws_use_package(aws-c-auth)
aws_use_package(aws-c-common)
aws_use_package(aws-c-io)

target_link_libraries(${PROJECT_NAME} ${DEP_AWS_LIBS})

install(FILES ${AWS_CRT_HEADERS} DESTINATION "include/aws/crt" COMPONENT Development)
install(FILES ${AWS_CRT_AUTH_HEADERS} DESTINATION "include/aws/crt/auth" COMPONENT Development)
install(FILES ${AWS_CRT_CRYPTO_HEADERS} DESTINATION "include/aws/crt/crypto" COMPONENT Development)
install(FILES ${AWS_CRT_IO_HEADERS} DESTINATION "include/aws/crt/io" COMPONENT Development)
install(FILES ${AWS_CRT_IOT_HEADERS} DESTINATION "include/aws/iot" COMPONENT Development)
install(FILES ${AWS_CRT_MQTT_HEADERS} DESTINATION "include/aws/crt/mqtt" COMPONENT Development)
install(FILES ${AWS_CRT_HTTP_HEADERS} DESTINATION "include/aws/crt/http" COMPONENT Development)
install(FILES ${AWS_CRT_EXTERNAL_HEADERS} DESTINATION "include/aws/crt/external" COMPONENT Development)

install(
        TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}-targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Development
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Development
        RUNTIME DESTINATION bin COMPONENT Runtime
)

if (BUILD_SHARED_LIBS)
    set (TARGET_DIR "shared")
else()
    set (TARGET_DIR "static")
endif()

install(EXPORT "${PROJECT_NAME}-targets"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}/cmake/${TARGET_DIR}"
        NAMESPACE AWS::
        COMPONENT Development)

configure_file("cmake/${PROJECT_NAME}-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
        @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}/cmake/"
        COMPONENT Development)

if (NOT CMAKE_CROSSCOMPILING)
    if (BUILD_TESTING)
        add_subdirectory(tests)
        add_subdirectory(bin/elasticurl_cpp)
    endif()
endif()
