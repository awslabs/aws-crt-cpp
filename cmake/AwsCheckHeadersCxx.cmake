# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0.

# This cmake logic verifies that each of our headers is complete, in that it
# includes any necessary dependencies, and builds with each supported c++ standard.
#
# To do so, we generate a single-line C or C++ source file that includes each
# header, and link all of these stub source files into a test executable.

option(PERFORM_HEADER_CHECK_CXX "Performs compile-time checks that each header can be included independently.")

# Call as: aws_check_headers_cxx(${target} HEADERS TO CHECK LIST)
function(aws_check_headers_cxx target)
    # Check headers against each supported CXX_STANDARD
    if (PERFORM_HEADER_CHECK_CXX)
        aws_check_headers_cxx_internal(${target} 11 ${ARGN})
        aws_check_headers_cxx_internal(${target} 14 ${ARGN})
        aws_check_headers_cxx_internal(${target} 17 ${ARGN})
        aws_check_headers_cxx_internal(${target} 20 ${ARGN})
        aws_check_headers_cxx_internal(${target} 23 ${ARGN})
    endif ()
endfunction()

function(aws_check_headers_cxx_internal target std)
    # Check that compiler supports this std
    list (FIND CMAKE_CXX_COMPILE_FEATURES "cxx_std_${std}" feature_idx)
    if (${feature_idx} LESS 0)
        return()
    endif()

    # MSVC's c++ 20 has issues with templates
    if (MSVC AND NOT ${std} LESS 20)
        return()
    endif()

    set(HEADER_CHECKER_ROOT "${CMAKE_CURRENT_BINARY_DIR}/header-checker-cxx-${std}")

    # Write stub main file
    set(HEADER_CHECKER_MAIN "${HEADER_CHECKER_ROOT}/stub.cpp")
    file(WRITE ${HEADER_CHECKER_MAIN} "
        int main(int argc, char **argv) {
            (void)argc;
            (void)argv;

            return 0;
        }")

    set(HEADER_CHECKER_LIB ${target}-header-check-cxx-${std})
    add_executable(${HEADER_CHECKER_LIB} ${HEADER_CHECKER_MAIN})
    target_link_libraries(${HEADER_CHECKER_LIB} ${target})
    target_compile_definitions(${HEADER_CHECKER_LIB} PRIVATE AWS_UNSTABLE_TESTING_API=1 AWS_HEADER_CHECKER=1)

    set_target_properties(${HEADER_CHECKER_LIB} PROPERTIES
        LINKER_LANGUAGE CXX
        CXX_STANDARD ${std}
        CXX_STANDARD_REQUIRED OFF
    )

    foreach(header IN LISTS ARGN)
        if (NOT ${header} MATCHES "\\.inl$")
            file(RELATIVE_PATH rel_header ${CMAKE_HOME_DIRECTORY} ${header})
            file(RELATIVE_PATH include_path "${CMAKE_HOME_DIRECTORY}/include" ${header})
            set(stub_dir "${HEADER_CHECKER_ROOT}/${rel_header}")
            file(MAKE_DIRECTORY "${stub_dir}")
            # include file twice to ensure header guards are present
            file(WRITE "${stub_dir}/check.cpp" "#include <${include_path}>\n#include <${include_path}>\n")

            target_sources(${HEADER_CHECKER_LIB} PUBLIC "${stub_dir}/check.cpp")
        endif()
    endforeach(header)
endfunction()
