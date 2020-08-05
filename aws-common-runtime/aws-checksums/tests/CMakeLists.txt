include(AwsLibFuzzer)
include(AwsTestHarness)
enable_testing()

file(GLOB TEST_HDRS "*.h")
file(GLOB TEST_SRC "*.c")

file(GLOB TESTS ${TEST_HDRS} ${TEST_SRC})

add_test_case(test_crc32c)
add_test_case(test_crc32)

generate_test_driver(${PROJECT_NAME}-tests)
