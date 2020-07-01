/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/testing/aws_test_harness.h>

static int s_base64_round_trip(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        Aws::Crt::String test_data = "foobar";
        Aws::Crt::String expected = "Zm9vYmFy";

        const Aws::Crt::Vector<uint8_t> test_vector(test_data.begin(), test_data.end());
        const Aws::Crt::Vector<uint8_t> expected_vector(expected.begin(), expected.end());

        Aws::Crt::String encoded = Aws::Crt::Base64Encode(test_vector);
        ASSERT_BIN_ARRAYS_EQUALS(expected.data(), expected.size(), encoded.data(), encoded.size());

        Aws::Crt::Vector<uint8_t> decoded = Aws::Crt::Base64Decode(encoded);
        ASSERT_BIN_ARRAYS_EQUALS(test_vector.data(), test_vector.size(), decoded.data(), decoded.size());
    }

    return 0;
}

AWS_TEST_CASE(Base64RoundTrip, s_base64_round_trip)
