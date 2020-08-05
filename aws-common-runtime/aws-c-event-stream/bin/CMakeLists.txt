# Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
# SPDX-License-Identifier: Apache-2.0.

add_executable(aws-c-event-stream-pipe "event_stream_pipe.c")
aws_set_common_properties(aws-c-event-stream-pipe)
target_link_libraries(aws-c-event-stream-pipe ${PROJECT_NAME})
set_target_properties(aws-c-event-stream-pipe PROPERTIES LINKER_LANGUAGE C)
set_property(TARGET aws-c-event-stream-pipe PROPERTY C_STANDARD 99)

add_executable(aws-c-event-stream-write-test-case "event_stream_write_test_case.c")
aws_set_common_properties(aws-c-event-stream-write-test-case)
target_link_libraries(aws-c-event-stream-write-test-case ${PROJECT_NAME})
set_target_properties(aws-c-event-stream-write-test-case PROPERTIES LINKER_LANGUAGE C)
set_property(TARGET aws-c-event-stream-write-test-case PROPERTY C_STANDARD 99)
