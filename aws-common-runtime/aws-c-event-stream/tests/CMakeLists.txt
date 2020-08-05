# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0.
include(AwsTestHarness)
enable_testing()

file(GLOB TEST_SRC "*.c")
file(GLOB TESTS ${TEST_SRC})

set(TEST_BINARY_NAME ${PROJECT_NAME}-tests)

add_test_case(test_incoming_no_op_valid)
add_test_case(test_incoming_application_data_no_headers_valid)
add_test_case(test_incoming_application_one_compressed_header_pair_valid)
add_test_case(test_incoming_application_int32_header_valid)

add_test_case(test_outgoing_no_op_valid)
add_test_case(test_outgoing_application_data_no_headers_valid)
add_test_case(test_outgoing_application_one_compressed_header_pair_valid)

add_test_case(test_streaming_decoder_incoming_no_op_valid_single_message)
add_test_case(test_streaming_decoder_incoming_application_no_headers)
add_test_case(test_streaming_decoder_incoming_application_one_compressed_header_pair_valid)
add_test_case(test_streaming_decoder_incoming_multiple_messages)

generate_test_driver(${TEST_BINARY_NAME})
