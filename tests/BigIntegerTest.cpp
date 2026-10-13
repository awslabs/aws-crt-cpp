/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/BigInteger.h>
#include <aws/testing/aws_test_harness.h>

#include <utility>

using namespace Aws::Crt;

static const char *s_hugeInteger = "-123456789012345678901234567890123456789012345678901234567890";

static int s_BigIntegerParseAndFormatTest(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);

        auto huge = BigInteger::FromString(aws_byte_cursor_from_c_str(s_hugeInteger), allocator);
        ASSERT_TRUE(huge.has_value());
        ASSERT_TRUE(huge->ToString() == s_hugeInteger);

        // Leading zeros are dropped and zero is never negative.
        auto zero = BigInteger::FromString(aws_byte_cursor_from_c_str("-000"), allocator);
        ASSERT_TRUE(zero.has_value());
        ASSERT_TRUE(zero->ToString() == "0");

        auto int64Min = BigInteger::FromString(aws_byte_cursor_from_c_str("-9223372036854775808"), allocator);
        ASSERT_TRUE(int64Min.has_value());
        ASSERT_TRUE(int64Min->ToString() == "-9223372036854775808");
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BigIntegerParseAndFormatTest, s_BigIntegerParseAndFormatTest)

static int s_BigIntegerInvalidInputTest(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);

        auto bad = BigInteger::FromString(aws_byte_cursor_from_c_str("12x"), allocator);
        ASSERT_FALSE(bad.has_value());
        ASSERT_INT_EQUALS(AWS_ERROR_INVALID_ARGUMENT, LastError());

        auto empty = BigInteger::FromString(aws_byte_cursor_from_c_str(""), allocator);
        ASSERT_FALSE(empty.has_value());
        ASSERT_INT_EQUALS(AWS_ERROR_INVALID_ARGUMENT, LastError());

        auto padded = BigInteger::FromString(aws_byte_cursor_from_c_str(" 1"), allocator);
        ASSERT_FALSE(padded.has_value());
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BigIntegerInvalidInputTest, s_BigIntegerInvalidInputTest)

static int s_BigIntegerEqualityTest(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);

        auto a = BigInteger::FromString(aws_byte_cursor_from_c_str(s_hugeInteger), allocator);
        auto b = BigInteger::FromString(aws_byte_cursor_from_c_str(s_hugeInteger + 1), allocator);
        auto aAgain = BigInteger::FromString(
            aws_byte_cursor_from_c_str("-000123456789012345678901234567890123456789012345678901234567890"), allocator);
        ASSERT_TRUE(a.has_value() && b.has_value() && aAgain.has_value());

        ASSERT_TRUE(*a == *aAgain);
        ASSERT_TRUE(*a != *b);

        auto zero = BigInteger::FromString(aws_byte_cursor_from_c_str("0"), allocator);
        auto negativeZero = BigInteger::FromString(aws_byte_cursor_from_c_str("-0"), allocator);
        ASSERT_TRUE(zero.has_value() && negativeZero.has_value());
        ASSERT_TRUE(*zero == *negativeZero);
        ASSERT_TRUE(*zero != *a);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BigIntegerEqualityTest, s_BigIntegerEqualityTest)

static int s_BigIntegerCopyAndMoveTest(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);

        auto other = BigInteger::FromString(aws_byte_cursor_from_c_str("42"), allocator);
        ASSERT_TRUE(other.has_value());
        BigInteger assigned = *other;

        Optional<BigInteger> copied;
        {
            auto original = BigInteger::FromString(aws_byte_cursor_from_c_str(s_hugeInteger), allocator);
            ASSERT_TRUE(original.has_value());

            copied = *original;
            assigned = *original;
            ASSERT_TRUE(*copied == *original);
            ASSERT_TRUE(assigned == *original);
        }

        // Copies outlive the value they were copied from.
        ASSERT_TRUE(copied->ToString() == s_hugeInteger);
        ASSERT_TRUE(assigned.ToString() == s_hugeInteger);
        ASSERT_TRUE(other->ToString() == "42");

        BigInteger moved = std::move(*copied);
        ASSERT_TRUE(moved == assigned);
        ASSERT_TRUE(moved.ToString() == s_hugeInteger);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BigIntegerCopyAndMoveTest, s_BigIntegerCopyAndMoveTest)

static int s_BigIntegerDefaultTest(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);

        auto zero = BigInteger::FromString(aws_byte_cursor_from_c_str("0"), allocator);
        ASSERT_TRUE(zero.has_value());

        BigInteger defaulted;
        ASSERT_TRUE(defaulted.ToString() == "0");
        ASSERT_TRUE(defaulted == *zero);

        BigInteger withAllocator(allocator);
        ASSERT_TRUE(withAllocator.ToString() == "0");
        ASSERT_TRUE(withAllocator == *zero);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BigIntegerDefaultTest, s_BigIntegerDefaultTest)
