cmake_minimum_required(VERSION 3.1)

if(POLICY CMP0077)
    cmake_policy(SET CMP0077 NEW)
endif()

option(BUILD_DEPS "Builds aws common runtime dependencies as part of build. Turn off if you want to control your dependency chain." ON)
option(BYO_CRYPTO "Don't build a tls implementation or link against a crypto interface. This feature is only for unix builds currently" OFF)
option(USE_OPENSSL "Set this if you want to use your system's OpenSSL 1.0.2/1.1.1 compatible libcrypto" OFF)

# Let aws-iot-device-sdk-cpp-v2 report its own version in MQTT connections (instead of reporting aws-crt-cpp's version).
option(AWS_IOT_SDK_VERSION "Set the version reported by Aws::Iot::MqttClientConnectionConfigBuilder")

# Tests require environment variables setup in order to run properly.
# See https://github.com/awslabs/aws-crt-builder/blob/main/builder/actions/setup_cross_ci_crt_environment.py
# for how environment variables are setup.
# NOTE: Some environment variables use Mosquitto or Proxy servers, which are assumed to be installed
# locally if running the testing outside of CI/CD.
option(ENABLE_PROXY_INTEGRATION_TESTS "Whether or not to build and run the proxy integration tests that rely on a proxy server installed and running locally" OFF)


list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(AwsGetVersion)
aws_get_version(SIMPLE_VERSION FULL_VERSION GIT_HASH)
message(STATUS "AWS CRT C++ ${FULL_VERSION}")

project("aws-crt-cpp"
    LANGUAGES CXX C
    VERSION ${SIMPLE_VERSION})

include(CTest)

if(DEFINED CMAKE_PREFIX_PATH)
    file(TO_CMAKE_PATH "${CMAKE_PREFIX_PATH}" CMAKE_PREFIX_PATH)
endif()

if(DEFINED CMAKE_INSTALL_PREFIX)
    file(TO_CMAKE_PATH "${CMAKE_INSTALL_PREFIX}" CMAKE_INSTALL_PREFIX)
endif()

if(UNIX AND NOT APPLE)
    include(GNUInstallDirs)
elseif(NOT DEFINED CMAKE_INSTALL_LIBDIR)
    set(CMAKE_INSTALL_LIBDIR "lib")
endif()

if(${CMAKE_INSTALL_LIBDIR} STREQUAL "lib64")
    set(FIND_LIBRARY_USE_LIB64_PATHS true)
endif()

if(NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 11)
endif()

if(NOT CMAKE_BUILD_TYPE)
    # setting this breaks C++ builds in visualc++, so don't do it.
    if(NOT WIN32)
        set(CMAKE_BUILD_TYPE "RelWithDebInfo")
    endif()
endif()

set(GENERATED_ROOT_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(GENERATED_INCLUDE_DIR "${GENERATED_ROOT_DIR}/include")
set(GENERATED_CONFIG_HEADER "${GENERATED_INCLUDE_DIR}/aws/crt/Config.h")
configure_file(include/aws/crt/Config.h.in ${GENERATED_CONFIG_HEADER} @ONLY)

# This is required in order to append /lib/cmake to each element in CMAKE_PREFIX_PATH
set(AWS_MODULE_DIR "/${CMAKE_INSTALL_LIBDIR}/cmake")
string(REPLACE ";" "${AWS_MODULE_DIR};" AWS_MODULE_PATH "${CMAKE_PREFIX_PATH}${AWS_MODULE_DIR}")

# Append that generated list to the module search path
list(APPEND CMAKE_MODULE_PATH ${AWS_MODULE_PATH})

if(BUILD_DEPS)
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/crt/aws-c-common/cmake")

    include(AwsFindPackage)

    set(IN_SOURCE_BUILD ON)
    set(BUILD_TESTING_PREV ${BUILD_TESTING})
    set(BUILD_TESTING OFF)
    add_subdirectory(crt/aws-c-common)

    if(UNIX AND NOT APPLE AND NOT BYO_CRYPTO)
        if(NOT USE_OPENSSL)
            set(DISABLE_PERL ON CACHE BOOL "Disable Perl for AWS-LC.")
            set(DISABLE_GO ON CACHE BOOL "Disable Go for AWS-LC.")
            set(BUILD_LIBSSL OFF CACHE BOOL "Build libssl for AWS-LC.")

            # temporarily disable certain warnings as errors for the aws-lc build
            set(OLD_CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")

            if(NOT MSVC)
                check_c_compiler_flag(-Wno-stringop-overflow HAS_WNO_STRINGOP_OVERFLOW)

                if(HAS_WNO_STRINGOP_OVERFLOW)
                    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-stringop-overflow")
                endif()

                check_c_compiler_flag(-Wno-array-parameter HAS_WNO_ARRAY_PARAMETER)

                if(HAS_WNO_ARRAY_PARAMETER)
                    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wno-array-parameter")
                endif()
            endif()

            add_subdirectory(crt/aws-lc)

            # restore previous build flags
            set(CMAKE_C_FLAGS "${OLD_CMAKE_C_FLAGS}")

            set(SEARCH_LIBCRYPTO OFF CACHE BOOL "Let S2N use libcrypto from AWS-LC.")
        else()
            set(SEARCH_LIBCRYPTO ON CACHE BOOL "Let S2N search libcrypto in the system.")
        endif()

        set(UNSAFE_TREAT_WARNINGS_AS_ERRORS OFF CACHE BOOL "Disable warnings-as-errors when building S2N")
        add_subdirectory(crt/s2n)
    endif()

    add_subdirectory(crt/aws-c-sdkutils)
    add_subdirectory(crt/aws-c-io)
    add_subdirectory(crt/aws-c-cal)
    add_subdirectory(crt/aws-c-compression)
    add_subdirectory(crt/aws-c-http)
    add_subdirectory(crt/aws-c-auth)
    add_subdirectory(crt/aws-c-mqtt)
    add_subdirectory(crt/aws-checksums)
    add_subdirectory(crt/aws-c-event-stream)
    add_subdirectory(crt/aws-c-s3)
    set(BUILD_TESTING ${BUILD_TESTING_PREV})
else()
    include(AwsFindPackage)
    set(IN_SOURCE_BUILD OFF)
endif()

include(AwsCFlags)
include(AwsSharedLibSetup)

file(GLOB AWS_CRT_HEADERS
    "include/aws/crt/*.h"
    ${GENERATED_CONFIG_HEADER}
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

file(GLOB AWS_CRT_ENDPOINT_HEADERS
    "include/aws/crt/endpoints/*.h"
)

file(GLOB AWS_CRT_CBOR_HEADERS
    "include/aws/crt/cbor/*.h"
)

file(GLOB AWS_CRT_PUBLIC_HEADERS
    ${AWS_CRT_HEADERS}
    ${AWS_CRT_AUTH_HEADERS}
    ${AWS_CRT_CRYPTO_HEADERS}
    ${AWS_CRT_IO_HEADERS}
    ${AWS_CRT_IOT_HEADERS}
    ${AWS_CRT_MQTT_HEADERS}
    ${AWS_CRT_HTTP_HEADERS}
    ${AWS_CRT_ENDPOINT_HEADERS}
    ${AWS_CRT_CBOR_HEADERS}
)

if(BUILD_DEPS)
    include(AwsCheckHeaders)
    aws_check_headers(${PROJECT_NAME} IS_CXX ${AWS_CRT_PUBLIC_HEADERS})
endif()

file(GLOB AWS_CRT_CPP_HEADERS
    ${AWS_CRT_PUBLIC_HEADERS}
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

file(GLOB AWS_CRT_IO_SRC
    "source/io/*.cpp"
)

file(GLOB AWS_CRT_IOT_SRC
    "source/iot/*.cpp"
)

file(GLOB AWS_CRT_MQTT_SRC
    "source/mqtt/*.cpp"
)

file(GLOB AWS_CRT_HTTP_SRC
    "source/http/*.cpp"
)

file(GLOB AWS_CRT_ENDPOINTS_SRC
    "source/endpoints/*.cpp"
)

file(GLOB AWS_CRT_CBOR_SRC
    "source/cbor/*.cpp"
)

file(GLOB AWS_CRT_CPP_SRC
    ${AWS_CRT_SRC}
    ${AWS_CRT_AUTH_SRC}
    ${AWS_CRT_CRYPTO_SRC}
    ${AWS_CRT_IO_SRC}
    ${AWS_CRT_IOT_SRC}
    ${AWS_CRT_MQTT_SRC}
    ${AWS_CRT_HTTP_SRC}
    ${AWS_CRT_ENDPOINTS_SRC}
    ${AWS_CRT_CBOR_SRC}
)

if(WIN32)
    if(MSVC)
        source_group("Header Files\\aws\\crt" FILES ${AWS_CRT_HEADERS})
        source_group("Header Files\\aws\\crt\\auth" FILES ${AWS_CRT_AUTH_HEADERS})
        source_group("Header Files\\aws\\crt\\crypto" FILES ${AWS_CRT_CRYPTO_HEADERS})
        source_group("Header Files\\aws\\crt\\io" FILES ${AWS_CRT_IO_HEADERS})
        source_group("Header Files\\aws\\iot" FILES ${AWS_CRT_IOT_HEADERS})
        source_group("Header Files\\aws\\crt\\mqtt" FILES ${AWS_CRT_MQTT_HEADERS})
        source_group("Header Files\\aws\\crt\\http" FILES ${AWS_CRT_HTTP_HEADERS})
        source_group("Header Files\\aws\\crt\\endpoints" FILES ${AWS_CRT_ENDPOINT_HEADERS})
        source_group("Header Files\\aws\\crt\\cbor" FILES ${AWS_CRT_CBOR_HEADERS})

        source_group("Source Files" FILES ${AWS_CRT_SRC})
        source_group("Source Files\\auth" FILES ${AWS_CRT_AUTH_SRC})
        source_group("Source Files\\crypto" FILES ${AWS_CRT_CRYPTO_SRC})
        source_group("Source Files\\io" FILES ${AWS_CRT_IO_SRC})
        source_group("Source Files\\iot" FILES ${AWS_CRT_IOT_SRC})
        source_group("Source Files\\mqtt" FILES ${AWS_CRT_MQTT_SRC})
        source_group("Source Files\\http" FILES ${AWS_CRT_HTTP_SRC})
        source_group("Source Files\\endpoints" FILES ${AWS_CRT_ENDPOINTS_SRC})
        source_group("Source Files\\cbor" FILES ${AWS_CRT_CBOR_SRC})
    endif()
endif()

add_library(${PROJECT_NAME} ${AWS_CRT_CPP_HEADERS} ${AWS_CRT_CPP_SRC})

target_compile_definitions(${PROJECT_NAME} PRIVATE -DCJSON_HIDE_SYMBOLS)

if(AWS_IOT_SDK_VERSION)
    target_compile_definitions(${PROJECT_NAME} PRIVATE "-DAWS_IOT_SDK_VERSION=\"${AWS_IOT_SDK_VERSION}\"")
endif()

if(BUILD_SHARED_LIBS)
    target_compile_definitions(${PROJECT_NAME} PUBLIC -DAWS_CRT_CPP_USE_IMPORT_EXPORT)
    target_compile_definitions(${PROJECT_NAME} PRIVATE -DAWS_CRT_CPP_EXPORTS)
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)
set_target_properties(${PROJECT_NAME} PROPERTIES CXX_STANDARD ${CMAKE_CXX_STANDARD})

aws_prepare_symbol_visibility_args(${PROJECT_NAME} "AWS_CRT_CPP")

# set runtime library
if(MSVC)
    if(AWS_STATIC_MSVC_RUNTIME_LIBRARY OR STATIC_CRT)
        target_compile_options(${PROJECT_NAME} PRIVATE "/MT$<$<CONFIG:Debug>:d>")
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE "/MD$<$<CONFIG:Debug>:d>")
    endif()
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE $<$<CONFIG:Debug>:DEBUG_BUILD>)

# set extra warning flags
if(AWS_WARNINGS_ARE_ERRORS)
    if(MSVC)
        target_compile_options(${PROJECT_NAME} PRIVATE /W4 /WX /wd4068)
    else()
        target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wno-long-long -pedantic -Werror)
    endif()
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<BUILD_INTERFACE:${GENERATED_INCLUDE_DIR}>
    $<INSTALL_INTERFACE:include>)

aws_use_package(aws-c-http)
aws_use_package(aws-c-mqtt)
aws_use_package(aws-c-cal)
aws_use_package(aws-c-auth)
aws_use_package(aws-c-common)
aws_use_package(aws-c-io)
aws_use_package(aws-checksums)
aws_use_package(aws-c-event-stream)
aws_use_package(aws-c-s3)

include(AwsSanitizers)
aws_add_sanitizers(${PROJECT_NAME})

target_link_libraries(${PROJECT_NAME} PUBLIC ${DEP_AWS_LIBS})

install(FILES ${AWS_CRT_HEADERS} DESTINATION "include/aws/crt" COMPONENT Development)
install(FILES ${AWS_CRT_AUTH_HEADERS} DESTINATION "include/aws/crt/auth" COMPONENT Development)
install(FILES ${AWS_CRT_CRYPTO_HEADERS} DESTINATION "include/aws/crt/crypto" COMPONENT Development)
install(FILES ${AWS_CRT_IO_HEADERS} DESTINATION "include/aws/crt/io" COMPONENT Development)
install(FILES ${AWS_CRT_IOT_HEADERS} DESTINATION "include/aws/iot" COMPONENT Development)
install(FILES ${AWS_CRT_MQTT_HEADERS} DESTINATION "include/aws/crt/mqtt" COMPONENT Development)
install(FILES ${AWS_CRT_HTTP_HEADERS} DESTINATION "include/aws/crt/http" COMPONENT Development)
install(FILES ${AWS_CRT_ENDPOINT_HEADERS} DESTINATION "include/aws/crt/endpoints" COMPONENT Development)
install(FILES ${AWS_CRT_CBOR_HEADERS} DESTINATION "include/aws/crt/cbor" COMPONENT Development)

install(
    TARGETS ${PROJECT_NAME}
    EXPORT ${PROJECT_NAME}-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Development
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR} COMPONENT Development
    RUNTIME DESTINATION bin COMPONENT Runtime
)

if(BUILD_SHARED_LIBS)
    set(TARGET_DIR "shared")
else()
    set(TARGET_DIR "static")
endif()

install(EXPORT "${PROJECT_NAME}-targets"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}/cmake/${TARGET_DIR}"
    NAMESPACE AWS::
    COMPONENT Development)

configure_file("cmake/${PROJECT_NAME}-config.cmake"
    "${GENERATED_ROOT_DIR}/${PROJECT_NAME}-config.cmake"
    @ONLY)

include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    "${GENERATED_ROOT_DIR}/${PROJECT_NAME}-config-version.cmake"
    COMPATIBILITY ExactVersion
)

install(FILES "${GENERATED_ROOT_DIR}/${PROJECT_NAME}-config.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}/cmake/"
    COMPONENT Development)

install(FILES "${GENERATED_ROOT_DIR}/${PROJECT_NAME}-config-version.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/${PROJECT_NAME}/cmake/"
    COMPONENT Development)

if(NOT CMAKE_CROSSCOMPILING)
    if(BUILD_TESTING)
        add_subdirectory(tests)

        if(NOT BYO_CRYPTO)
            add_subdirectory(bin/elasticurl_cpp)
            add_subdirectory(bin/mqtt5_app)
            add_subdirectory(bin/mqtt5_canary)
        endif()
    endif()
endif()
