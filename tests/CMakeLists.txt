include(AwsTestHarness)
enable_testing()
include(CTest)

file(GLOB TEST_SRC "*.cpp")
file(GLOB TEST_HDRS "*.h")
file(GLOB TESTS ${TEST_HDRS} ${TEST_SRC})

set(TEST_BINARY_NAME ${CMAKE_PROJECT_NAME}-tests)

add_test_case(EventLoopResourceSafety)
add_test_case(MqttClientResourceSafety)
add_test_case(ClientBootstrapResourceSafety)
add_test_case(TLSContextResourceSafety)

generate_cpp_test_driver(${TEST_BINARY_NAME})

