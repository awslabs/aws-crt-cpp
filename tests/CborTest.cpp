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
    /**
     * Simply test every method works.
     */
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);
        Cbor::CborEncoder encoder(allocator);

        // Define expected values
        auto expected_uint_val = 42ULL;
        auto expected_negint_val = 123ULL;
        auto expected_float_val = 3.14;
        ByteCursor expected_bytes_val = aws_byte_cursor_from_c_str("write more");
        ByteCursor expected_text_val = aws_byte_cursor_from_c_str("test");
        size_t expected_array_size = 5;
        size_t expected_map_size = 3;
        auto expected_tag_val = 999ULL;
        bool expected_bool_val = true;

        encoder.WriteUint(expected_uint_val);
        encoder.WriteNegInt(expected_negint_val);
        encoder.WriteFloat(expected_float_val);
        encoder.WriteBytes(expected_bytes_val);
        encoder.WriteText(expected_text_val);
        encoder.WriteArrayStart(expected_array_size);
        encoder.WriteMapStart(expected_map_size);
        encoder.WriteTag(expected_tag_val);
        encoder.WriteBool(expected_bool_val);
        encoder.WriteNull();
        encoder.WriteUndefined();
        encoder.WriteBreak();
        encoder.WriteIndefBytesStart();
        encoder.WriteIndefTextStart();
        encoder.WriteIndefArrayStart();
        encoder.WriteIndefMapStart();

        ByteCursor cursor = encoder.GetEncodedData();

        Cbor::CborDecoder decoder(allocator, cursor);
        Cbor::CborType out_type = Cbor::CborType::Unknown;

        // Check for Uint
        uint64_t uint_val;
        ASSERT_TRUE(decoder.PeekType(out_type) && out_type == Cbor::CborType::Uint);
        ASSERT_TRUE(decoder.PopNextUnsignedIntVal(uint_val) && uint_val == expected_uint_val);

        // Check for NegInt
        uint64_t negint_val;
        ASSERT_TRUE(decoder.PeekType(out_type) && out_type == Cbor::CborType::NegInt);
        ASSERT_TRUE(decoder.PopNextNegativeIntVal(negint_val) && negint_val == expected_negint_val);

        // Check for Float
        double float_val;
        ASSERT_TRUE(decoder.PeekType(out_type) && out_type == Cbor::CborType::Float);
        ASSERT_TRUE(decoder.PopNextFloatVal(float_val) && float_val == expected_float_val);

        // Check for Bytes
        ByteCursor bytes_val;
        ASSERT_TRUE(decoder.PeekType(out_type) && out_type == Cbor::CborType::Bytes);
        ASSERT_TRUE(decoder.PopNextBytesVal(bytes_val) && aws_byte_cursor_eq(&bytes_val, &expected_bytes_val));

        // Check for Text
        ByteCursor text_val;
        ASSERT_TRUE(decoder.PeekType(out_type) && out_type == Cbor::CborType::Text);
        ASSERT_TRUE(decoder.PopNextTextVal(text_val) && aws_byte_cursor_eq(&text_val, &expected_text_val));

        // Check for ArrayStart
        uint64_t array_size;
        ASSERT_TRUE(decoder.PeekType(out_type) && out_type == Cbor::CborType::ArrayStart);
        ASSERT_TRUE(decoder.PopNextArrayStart(array_size) && array_size == expected_array_size);

        // Check for MapStart
        uint64_t map_size;
        ASSERT_TRUE(decoder.PeekType(out_type) && out_type == Cbor::CborType::MapStart);
        ASSERT_TRUE(decoder.PopNextMapStart(map_size) && map_size == expected_map_size);

        // Check for Tag
        uint64_t tag_val;
        ASSERT_TRUE(decoder.PeekType(out_type) && out_type == Cbor::CborType::Tag);
        ASSERT_TRUE(decoder.PopNextTagVal(tag_val) && tag_val == expected_tag_val);

        // Check for Bool
        bool bool_val;
        ASSERT_TRUE(decoder.PeekType(out_type) && out_type == Cbor::CborType::Bool);
        ASSERT_TRUE(decoder.PopNextBooleanVal(bool_val) && bool_val == expected_bool_val);

        // Check for Null
        ASSERT_TRUE(decoder.PeekType(out_type) && out_type == Cbor::CborType::Null);
        ASSERT_TRUE(decoder.ConsumeNextWholeDataItem());

        // Check for Undefined
        ASSERT_TRUE(decoder.PeekType(out_type) && out_type == Cbor::CborType::Undefined);
        ASSERT_TRUE(decoder.ConsumeNextWholeDataItem());

        // Check for Break
        ASSERT_TRUE(decoder.PeekType(out_type) && out_type == Cbor::CborType::Break);
        ASSERT_TRUE(decoder.ConsumeNextSingleElement());

        // Check for IndefBytesStart
        ASSERT_TRUE(decoder.PeekType(out_type) && out_type == Cbor::CborType::IndefBytesStart);
        ASSERT_TRUE(decoder.ConsumeNextSingleElement());

        // Check for IndefTextStart
        ASSERT_TRUE(decoder.PeekType(out_type) && out_type == Cbor::CborType::IndefTextStart);
        ASSERT_TRUE(decoder.ConsumeNextSingleElement());

        // Check for IndefArrayStart
        ASSERT_TRUE(decoder.PeekType(out_type) && out_type == Cbor::CborType::IndefArrayStart);
        ASSERT_TRUE(decoder.ConsumeNextSingleElement());

        // Check for IndefMapStart
        ASSERT_TRUE(decoder.PeekType(out_type) && out_type == Cbor::CborType::IndefMapStart);
        ASSERT_TRUE(decoder.ConsumeNextSingleElement());

        auto length = decoder.GetRemainingLength();
        ASSERT_UINT_EQUALS(0, length);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(CborSanityTest, s_CborSanityTest)
