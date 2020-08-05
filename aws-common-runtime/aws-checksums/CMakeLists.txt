cmake_minimum_required (VERSION 3.1)

option(STATIC_CRT "Windows specific option that to specify static/dynamic run-time library" OFF)

project (aws-checksums C)

if (POLICY CMP0069)
    cmake_policy(SET CMP0069 NEW) # Enable LTO/IPO if available in the compiler, see AwsCFlags
endif()

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
include(AwsCheckHeaders)
include(AwsSharedLibSetup)
include(AwsSanitizers)
include(CheckCCompilerFlag)
include(AwsFindPackage)
include(AwsFeatureTests)

file(GLOB AWS_CHECKSUMS_HEADERS
     "include/aws/checksums/*.h"
)

file(GLOB AWS_CHECKSUMS_PRIV_HEADERS
     "include/aws/checksums/private/*.h"
)

file(GLOB AWS_CHECKSUMS_SRC
    "source/*.c"
)

if(MSVC)
     source_group("Header Files\\aws\\checksums" FILES ${AWS_CHECKSUMS_HEADERS})
     source_group("Source Files" FILES ${AWS_CHECKSUMS_SRC})
endif()

file(GLOB AWS_ARCH_SRC
        "source/generic/*.c"
        )

if (MSVC AND AWS_ARCH_INTEL)
    file(GLOB AWS_ARCH_SRC
            "source/intel/visualc/*.c"
    )

    source_group("Source Files\\intel\\visualc" FILES ${AWS_ARCH_SRC})

elseif(AWS_ARCH_INTEL AND AWS_HAVE_GCC_INLINE_ASM)
    file(GLOB AWS_ARCH_SRC
            "source/intel/asm/*.c"
        )
endif()

if (MSVC AND AWS_ARCH_ARM64)
    file(GLOB AWS_ARCH_SRC
        "source/arm/*.c"
        )
    source_group("Source Files\\arm" FILES ${AWS_ARCH_SRC})

elseif (AWS_ARCH_ARM64)
    file(GLOB AWS_ARCH_SRC
        "source/arm/*.c"
        )
     SET_SOURCE_FILES_PROPERTIES(source/arm/crc32c_arm.c PROPERTIES COMPILE_FLAGS -march=armv8-a+crc )
elseif ((NOT MSVC) AND AWS_ARCH_ARM32)
    set(CMAKE_REQUIRED_FLAGS "-march=armv8-a+crc")
        check_c_source_compiles("
#include <arm_acle.h>
int main() {
    int crc = __crc32d(0, 1);
    return 0;
    }" AWS_ARM32_CRC)
    unset(CMAKE_REQUIRED_FLAGS)
    if (AWS_ARM32_CRC)
        file(GLOB AWS_ARCH_SRC
            "source/arm/*.c"
            "source/arm/asm/*.c"
            )
         SET_SOURCE_FILES_PROPERTIES(source/arm/crc32c_arm.c PROPERTIES COMPILE_FLAGS -march=armv8-a+crc )
    endif()
endif()

file(GLOB CHECKSUMS_COMBINED_HEADERS
    ${AWS_CHECKSUMS_HEADERS}
    ${AWS_CHECKSUMS_PRIV_HEADERS}
)

file(GLOB CHECKSUMS_COMBINED_SRC
    ${AWS_CHECKSUMS_SRC}
    ${AWS_CHECKSUMS_PLATFORM_SOURCE}
    ${AWS_ARCH_SRC}
)


add_library(${PROJECT_NAME} ${CHECKSUMS_COMBINED_HEADERS} ${CHECKSUMS_COMBINED_SRC})
aws_set_common_properties(${PROJECT_NAME})
aws_prepare_symbol_visibility_args(${PROJECT_NAME} "AWS_CHECKSUMS")
aws_check_headers(${PROJECT_NAME} ${AWS_CHECKSUMS_HEADERS})

aws_add_sanitizers(${PROJECT_NAME})

# We are not ABI stable yet
set_target_properties(${PROJECT_NAME} PROPERTIES VERSION 1.0.0)
set_target_properties(${PROJECT_NAME} PROPERTIES SOVERSION 0unstable)

target_include_directories(${PROJECT_NAME} PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>)

aws_use_package(aws-c-common)

target_link_libraries(${PROJECT_NAME} PUBLIC ${DEP_AWS_LIBS})
aws_prepare_shared_lib_exports(${PROJECT_NAME})

install(FILES ${AWS_CHECKSUMS_HEADERS} DESTINATION "include/aws/checksums" COMPONENT Development)

if (BUILD_SHARED_LIBS)
    set (TARGET_DIR "shared")
else()
    set (TARGET_DIR "static")
endif()

install(EXPORT "${PROJECT_NAME}-targets"
        DESTINATION "${LIBRARY_DIRECTORY}/${PROJECT_NAME}/cmake/${TARGET_DIR}"
        NAMESPACE AWS::
        COMPONENT Development)

configure_file("cmake/${PROJECT_NAME}-config.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
        @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake"
        DESTINATION "${LIBRARY_DIRECTORY}/${PROJECT_NAME}/cmake/"
        COMPONENT Development)

include(CTest)
if (BUILD_TESTING)
    add_subdirectory(tests)
endif ()
