/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/cbor/Cbor.h>
#include <aws/testing/aws_test_harness.h>

using namespace Aws::Crt;

static int s_CborSanityTest(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);
        Cbor::CborEncoder encoder(allocator);
        uint64_t expected_val = 100;
        encoder.WriteUint(expected_val);
        ByteCursor cursor = encoder.GetEncodedData();

        Cbor::CborDecoder decoder(allocator, cursor);
        Cbor::CborType out_type = Cbor::CborType::Unknown;
        ASSERT_TRUE(decoder.PeekType(out_type));
        ASSERT_UINT_EQUALS((int)out_type, (int)Cbor::CborType::Uint);
        uint64_t out_val = 0;
        ASSERT_TRUE(decoder.PopNextUnsignedIntVal(out_val));
        ASSERT_UINT_EQUALS(expected_val, out_val);

        auto length = decoder.GetRemainingLength();
        ASSERT_UINT_EQUALS(0, length);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(CborSanityTest, s_CborSanityTest)
