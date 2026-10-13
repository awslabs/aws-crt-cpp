/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/cbor/Cbor.h>
#include <aws/testing/aws_test_harness.h>

#include <cmath>

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

        encoder.WriteUInt(expected_uint_val);
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

        Cbor::CborDecoder decoder(cursor, allocator);

        // Check for UInt
        ASSERT_TRUE(decoder.PeekType().value() == Cbor::CborType::UInt);
        ASSERT_TRUE(decoder.PopNextUnsignedIntVal().value() == expected_uint_val);

        // Check for NegInt
        ASSERT_TRUE(decoder.PeekType().value() == Cbor::CborType::NegInt);
        ASSERT_TRUE(decoder.PopNextNegativeIntVal().value() == expected_negint_val);

        // Check for Float
        ASSERT_TRUE(decoder.PeekType().value() == Cbor::CborType::Float);
        ASSERT_TRUE(decoder.PopNextFloatVal().value() == expected_float_val);

        // Check for Bytes
        ASSERT_TRUE(decoder.PeekType().value() == Cbor::CborType::Bytes);
        ByteCursor decoded_val = decoder.PopNextBytesVal().value();
        ASSERT_TRUE(aws_byte_cursor_eq(&decoded_val, &expected_bytes_val));

        // Check for Text
        ASSERT_TRUE(decoder.PeekType().value() == Cbor::CborType::Text);
        decoded_val = decoder.PopNextTextVal().value();
        ASSERT_TRUE(aws_byte_cursor_eq(&decoded_val, &expected_text_val));

        // Check for ArrayStart
        ASSERT_TRUE(decoder.PeekType().value() == Cbor::CborType::ArrayStart);
        ASSERT_TRUE(decoder.PopNextArrayStart().value() == expected_array_size);

        // Check for MapStart
        ASSERT_TRUE(decoder.PeekType().value() == Cbor::CborType::MapStart);
        ASSERT_TRUE(decoder.PopNextMapStart().value() == expected_map_size);
        // Check for Tag
        ASSERT_TRUE(decoder.PeekType().value() == Cbor::CborType::Tag);
        ASSERT_TRUE(decoder.PopNextTagVal().value() == expected_tag_val);

        // Check for Bool
        ASSERT_TRUE(decoder.PeekType().value() == Cbor::CborType::Bool);
        ASSERT_TRUE(decoder.PopNextBooleanVal().value() == expected_bool_val);

        // Check for Null
        ASSERT_TRUE(decoder.PeekType().value() == Cbor::CborType::Null);
        ASSERT_TRUE(decoder.ConsumeNextWholeDataItem());

        // Check for Undefined
        ASSERT_TRUE(decoder.PeekType().value() == Cbor::CborType::Undefined);
        ASSERT_TRUE(decoder.ConsumeNextWholeDataItem());

        // Check for Break
        ASSERT_TRUE(decoder.PeekType().value() == Cbor::CborType::Break);
        ASSERT_TRUE(decoder.ConsumeNextSingleElement());

        // Check for IndefBytesStart
        ASSERT_TRUE(decoder.PeekType().value() == Cbor::CborType::IndefBytesStart);
        ASSERT_TRUE(decoder.ConsumeNextSingleElement());

        // Check for IndefTextStart
        ASSERT_TRUE(decoder.PeekType().value() == Cbor::CborType::IndefTextStart);
        ASSERT_TRUE(decoder.ConsumeNextSingleElement());

        // Check for IndefArrayStart
        ASSERT_TRUE(decoder.PeekType().value() == Cbor::CborType::IndefArrayStart);
        ASSERT_TRUE(decoder.ConsumeNextSingleElement());

        // Check for IndefMapStart
        ASSERT_TRUE(decoder.PeekType().value() == Cbor::CborType::IndefMapStart);
        ASSERT_TRUE(decoder.ConsumeNextSingleElement());

        auto length = decoder.GetRemainingLength();
        ASSERT_UINT_EQUALS(0, length);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(CborSanityTest, s_CborSanityTest)

static void s_encode_timestamp_helper(
    Cbor::CborEncoder &encoder,
    const std::chrono::system_clock::time_point &timePoint) noexcept
{
    /* Get seconds with MS precision. */
    auto timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(timePoint.time_since_epoch());
    double seconds = static_cast<double>(timestampMs.count()) / 1000.0;

    encoder.WriteTag(AWS_CBOR_TAG_EPOCH_TIME);
    // Use the encoder to write the duration in seconds
    encoder.WriteFloat(seconds);
}

static int s_decode_timestamp_helper(Cbor::CborDecoder &decoder, std::chrono::system_clock::time_point &outTimePoint)
{
    if (decoder.PeekType().value() != Cbor::CborType::Tag)
    {
        return AWS_OP_ERR;
    }
    if (decoder.PopNextTagVal().value() != AWS_CBOR_TAG_EPOCH_TIME)
    {
        return AWS_OP_ERR;
    }
    Cbor::CborType out_type = decoder.PeekType().value();
    switch (out_type)
    {
        case Cbor::CborType::UInt:
        case Cbor::CborType::NegInt:
        {
            int64_t secs_timestamp = 0;
            uint64_t unsigned_val = 0;
            if (out_type == Cbor::CborType::UInt)
            {
                unsigned_val = decoder.PopNextUnsignedIntVal().value();
                if (unsigned_val > INT64_MAX)
                {
                    /* Overflow */
                    return AWS_OP_ERR;
                }
                secs_timestamp = (int64_t)unsigned_val;
            }
            else
            {
                unsigned_val = decoder.PopNextNegativeIntVal().value();
                if (unsigned_val > INT64_MAX)
                {
                    /* Overflow */
                    return AWS_OP_ERR;
                }
                /* convert the encoded number to real negative number */
                secs_timestamp = (int64_t)(-1 - unsigned_val);
            }
            std::chrono::duration<int64_t, std::chrono::seconds::period> timestamp(secs_timestamp);
            outTimePoint = std::chrono::system_clock::time_point(timestamp);
            return AWS_OP_SUCCESS;
        }
        case Cbor::CborType::Float:
        {
            double double_val = decoder.PopNextFloatVal().value();
            /* The timestamp has MS precision, but the double may not represent it exactly, so round to the nearest ms
             * instead of truncating. */
            std::chrono::milliseconds timestampMs(std::llround(double_val * 1000.0));
            outTimePoint = std::chrono::system_clock::time_point(timestampMs);
            return AWS_OP_SUCCESS;
        }
        default:
            break;
    }

    return AWS_OP_ERR;
}

static bool s_check_time_point_equals_ms_precision(
    const std::chrono::time_point<std::chrono::system_clock> &a,
    const std::chrono::time_point<std::chrono::system_clock> &b)
{
    using Ms = std::chrono::milliseconds;
    auto a_ms = std::chrono::duration_cast<Ms>(a.time_since_epoch());
    auto b_ms = std::chrono::duration_cast<Ms>(b.time_since_epoch());
    if (a_ms == b_ms)
    {
        return true;
    }
    else
    {
        return false;
    }
}

static int s_CborTimeStampTest(struct aws_allocator *allocator, void *ctx)
{
    /**
     * Example of how timestamp will be encoded and decoded with `std::chrono::system_clock::time_point`
     */
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);
        Cbor::CborEncoder encoder(allocator);
        const std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();

        s_encode_timestamp_helper(encoder, now);
        ByteCursor cursor = encoder.GetEncodedData();

        Cbor::CborDecoder decoder(cursor);
        std::chrono::time_point<std::chrono::system_clock> decoded;
        ASSERT_SUCCESS(s_decode_timestamp_helper(decoder, decoded));

        /* We only check the ms precision */
        ASSERT_TRUE(s_check_time_point_equals_ms_precision(decoded, now));
        auto length = decoder.GetRemainingLength();
        ASSERT_UINT_EQUALS(0, length);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(CborTimeStampTest, s_CborTimeStampTest)

static int s_CborLastErrorTest(struct aws_allocator *allocator, void *ctx)
{
    /**
     * Test the last error is initialzed correctly.
     */
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);
        Cbor::CborDecoder decoder({});
        ASSERT_INT_EQUALS(decoder.LastError(), AWS_ERROR_UNKNOWN);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(CborLastErrorTest, s_CborLastErrorTest)

static int s_CborBigIntegerTest(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);

        const char *inputs[] = {
            "0",
            "42",
            "-1",
            "18446744073709551615",  /* 2^64 - 1, largest plain UInt */
            "18446744073709551616",  /* 2^64, smallest positive bignum */
            "-18446744073709551616", /* -2^64, largest plain NegInt */
            "-18446744073709551617", /* -2^64 - 1, smallest negative bignum */
            "123456789012345678901234567890123456789012345678901234567890",
        };

        Cbor::CborEncoder encoder(allocator);
        for (const char *input : inputs)
        {
            encoder.WriteBigInteger(BigInteger::FromString(aws_byte_cursor_from_c_str(input), allocator).value());
        }

        Cbor::CborDecoder decoder(encoder.GetEncodedData(), allocator);
        for (const char *input : inputs)
        {
            auto value = decoder.PopNextBigIntegerVal();
            ASSERT_TRUE(value.has_value());
            ASSERT_TRUE(value->ToString() == input);
        }
        ASSERT_UINT_EQUALS(0, decoder.GetRemainingLength());

        // Only values outside the 64-bit range use a tag.
        Cbor::CborEncoder tagEncoder(allocator);
        tagEncoder.WriteBigInteger(BigInteger::FromString(aws_byte_cursor_from_c_str("42"), allocator).value());
        tagEncoder.WriteBigInteger(
            BigInteger::FromString(aws_byte_cursor_from_c_str("18446744073709551616"), allocator).value());
        Cbor::CborDecoder tagDecoder(tagEncoder.GetEncodedData(), allocator);
        ASSERT_TRUE(tagDecoder.PeekType().value() == Cbor::CborType::UInt);
        ASSERT_TRUE(tagDecoder.PopNextBigIntegerVal().has_value());
        ASSERT_TRUE(tagDecoder.PeekType().value() == Cbor::CborType::Tag);
        ASSERT_UINT_EQUALS(AWS_CBOR_TAG_UNSIGNED_BIGNUM, tagDecoder.PopNextTagVal().value());
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(CborBigIntegerTest, s_CborBigIntegerTest)

static int s_CborBigDecimalTest(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);

        const char *inputs[] = {
            "273.15",
            "1.50",
            "15e-1",
            "0e0",
            "-5e3",
            "-123456789012345678901234567890123456789012345678901234567890e-30",
            "1e-9223372036854775808",
            "1e9223372036854775807",
        };
        const char *expected[] = {
            "27315e-2",
            "150e-2",
            "15e-1",
            "0e0",
            "-5e3",
            "-123456789012345678901234567890123456789012345678901234567890e-30",
            "1e-9223372036854775808",
            "1e9223372036854775807",
        };

        Cbor::CborEncoder encoder(allocator);
        for (const char *input : inputs)
        {
            encoder.WriteBigDecimal(BigDecimal::FromString(aws_byte_cursor_from_c_str(input), allocator).value());
        }

        Cbor::CborDecoder decoder(encoder.GetEncodedData(), allocator);
        for (size_t i = 0; i < AWS_ARRAY_SIZE(inputs); ++i)
        {
            auto value = decoder.PopNextBigDecimalVal();
            ASSERT_TRUE(value.has_value(), "%s", inputs[i]);
            ASSERT_TRUE(value->ToString() == expected[i], "%s -> %s", inputs[i], value->ToString().c_str());
        }
        ASSERT_UINT_EQUALS(0, decoder.GetRemainingLength());

        // 273.15 is the RFC8949 section 3.4.4 example: tag 4, [-2, 27315].
        Cbor::CborEncoder rfcEncoder(allocator);
        rfcEncoder.WriteBigDecimal(BigDecimal::FromString(aws_byte_cursor_from_c_str("273.15"), allocator).value());
        const uint8_t rfcExpected[] = {0xc4, 0x82, 0x21, 0x19, 0x6a, 0xb3};
        ByteCursor rfcEncoded = rfcEncoder.GetEncodedData();
        ASSERT_BIN_ARRAYS_EQUALS(rfcExpected, sizeof(rfcExpected), rfcEncoded.ptr, rfcEncoded.len);

        Cbor::CborDecoder rfcDecoder(rfcEncoded, allocator);
        ASSERT_TRUE(rfcDecoder.PeekType().value() == Cbor::CborType::Tag);

        // The array may also be indefinite-length: tag 4, [_ -2, 27315], then an unsigned integer.
        const uint8_t indefinite[] = {0xc4, 0x9f, 0x21, 0x19, 0x6a, 0xb3, 0xff, 0x05};
        Cbor::CborDecoder indefiniteDecoder(aws_byte_cursor_from_array(indefinite, sizeof(indefinite)), allocator);
        auto indefiniteValue = indefiniteDecoder.PopNextBigDecimalVal();
        ASSERT_TRUE(indefiniteValue.has_value());
        ASSERT_TRUE(indefiniteValue->ToString() == "27315e-2");
        ASSERT_TRUE(indefiniteDecoder.PopNextUnsignedIntVal().value() == 5);

        // Failure sets LastError(): a plain integer is not a decimal fraction.
        Cbor::CborEncoder intEncoder(allocator);
        intEncoder.WriteUInt(1);
        Cbor::CborDecoder intDecoder(intEncoder.GetEncodedData(), allocator);
        ASSERT_FALSE(intDecoder.PopNextBigDecimalVal().has_value());
        ASSERT_INT_EQUALS(AWS_ERROR_CBOR_UNEXPECTED_TYPE, intDecoder.LastError());
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(CborBigDecimalTest, s_CborBigDecimalTest)
