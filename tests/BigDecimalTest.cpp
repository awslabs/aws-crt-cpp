/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/BigDecimal.h>
#include <aws/testing/aws_test_harness.h>

#include <utility>

using namespace Aws::Crt;

static Optional<BigDecimal> s_parse(const char *str, Allocator *allocator)
{
    return BigDecimal::FromString(aws_byte_cursor_from_c_str(str), allocator);
}

static int s_BigDecimalParseAndFormatTest(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);

        struct
        {
            const char *input;
            const char *expected;
        } cases[] = {
            {"0", "0e0"},
            {"-0", "0e0"},
            {"+1.0", "10e-1"},
            {"1.50", "150e-2"},
            {"-0.5", "-5e-1"},
            {"123.45E-2", "12345e-4"},
            {"1e+3", "1e3"},
            {"12345678901234567890.123456789", "12345678901234567890123456789e-9"},
            {"-123456789012345678901234567890123456789012345678901234567890e-5",
             "-123456789012345678901234567890123456789012345678901234567890e-5"},
            {"1e9223372036854775807", "1e9223372036854775807"},
        };

        for (const auto &c : cases)
        {
            auto value = s_parse(c.input, allocator);
            ASSERT_TRUE(value.has_value(), "failed to parse %s", c.input);
            ASSERT_TRUE(value->ToString() == c.expected, "%s -> %s", c.input, value->ToString().c_str());
        }
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BigDecimalParseAndFormatTest, s_BigDecimalParseAndFormatTest)

static int s_BigDecimalInvalidInputTest(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);

        for (const char *input : {"", "12x", " 1", "1.", ".5", "1e", "e5", "1e+"})
        {
            auto bad = s_parse(input, allocator);
            ASSERT_FALSE(bad.has_value(), "unexpectedly parsed '%s'", input);
            ASSERT_INT_EQUALS(AWS_ERROR_INVALID_ARGUMENT, LastError());
        }

        auto overflow = s_parse("1e9223372036854775808", allocator);
        ASSERT_FALSE(overflow.has_value());
        ASSERT_INT_EQUALS(AWS_ERROR_OVERFLOW_DETECTED, LastError());
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BigDecimalInvalidInputTest, s_BigDecimalInvalidInputTest)

static int s_BigDecimalEqualityTest(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);

        auto a = s_parse("-1.2345e10", allocator);
        auto aAgain = s_parse("-12345e6", allocator);
        auto differentMantissa = s_parse("1.2345e10", allocator);
        auto differentExponent = s_parse("-12345e7", allocator);
        ASSERT_TRUE(
            a.has_value() && aAgain.has_value() && differentMantissa.has_value() && differentExponent.has_value());

        ASSERT_TRUE(*a == *aAgain);
        ASSERT_TRUE(*a != *differentMantissa);
        ASSERT_TRUE(*a != *differentExponent);

        // Equality is structural: the same numeric value with a different scale is not equal.
        auto scaled = s_parse("1e3", allocator);
        auto unscaled = s_parse("1000", allocator);
        ASSERT_TRUE(scaled.has_value() && unscaled.has_value());
        ASSERT_TRUE(*scaled != *unscaled);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BigDecimalEqualityTest, s_BigDecimalEqualityTest)

static int s_BigDecimalCopyAndMoveTest(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);

        auto other = s_parse("1", allocator);
        ASSERT_TRUE(other.has_value());
        BigDecimal assigned = *other;

        Optional<BigDecimal> copied;
        {
            auto original = s_parse("-12345678901234567890.123456789", allocator);
            ASSERT_TRUE(original.has_value());

            copied = *original;
            assigned = *original;
            ASSERT_TRUE(*copied == *original);
            ASSERT_TRUE(assigned == *original);
        }

        // Copies outlive the value they were copied from.
        ASSERT_TRUE(copied->ToString() == "-12345678901234567890123456789e-9");
        ASSERT_TRUE(assigned.ToString() == "-12345678901234567890123456789e-9");
        ASSERT_TRUE(other->ToString() == "1e0");

        BigDecimal moved = std::move(*copied);
        ASSERT_TRUE(moved == assigned);
        ASSERT_TRUE(moved.ToString() == "-12345678901234567890123456789e-9");
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BigDecimalCopyAndMoveTest, s_BigDecimalCopyAndMoveTest)

static int s_BigDecimalDefaultTest(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);

        auto zero = s_parse("0", allocator);
        ASSERT_TRUE(zero.has_value());

        BigDecimal defaulted;
        ASSERT_TRUE(defaulted.ToString() == "0e0");
        ASSERT_TRUE(defaulted == *zero);

        BigDecimal withAllocator(allocator);
        ASSERT_TRUE(withAllocator.ToString() == "0e0");
        ASSERT_TRUE(withAllocator == *zero);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BigDecimalDefaultTest, s_BigDecimalDefaultTest)
