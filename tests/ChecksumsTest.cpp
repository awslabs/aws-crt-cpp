/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/checksums/Checksums.h>
#include <aws/testing/aws_test_harness.h>

static int s_testCrc32Zeroes(struct aws_allocator *allocator, void *)
{
    uint8_t zeroes[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t res = Aws::Crt::crc32(zeroes, 32, 0);
    uint32_t expected = 0x190A55AD;
    ASSERT_UINT_EQUALS(res, expected);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(testCrc32Zeroes, s_testCrc32Zeroes)

static int s_testCrc32ZeroesIterated(struct aws_allocator *allocator, void *)
{
    uint32_t res = 0;
    for (int i = 0; i < 32; i++)
    {
        uint8_t buf[] = {0};
        res = Aws::Crt::crc32(buf, 1, res);
    }
    uint32_t expected = 0x190A55AD;
    ASSERT_UINT_EQUALS(res, expected);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(testCrc32ZeroesIterated, s_testCrc32ZeroesIterated)

static int s_testCrc32Values(struct aws_allocator *allocator, void *)
{
    uint8_t values[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    uint32_t res = Aws::Crt::crc32(values, 32, 0);
    uint32_t expected = 0x91267E8A;
    ASSERT_UINT_EQUALS(res, expected);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(testCrc32Values, s_testCrc32Values)

static int s_testCrc32ValuesIterated(struct aws_allocator *allocator, void *)
{
    uint32_t res = 0;
    for (int i = 0; i < 32; i++)
    {
        uint8_t buf[] = {0};
        buf[0] = i;
        res = Aws::Crt::crc32(buf, 1, res);
    }
    uint32_t expected = 0x91267E8A;
    ASSERT_UINT_EQUALS(res, expected);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(testCrc32ValuesIterated, s_testCrc32ValuesIterated)

static int s_testCrc32LargeBuffer(struct aws_allocator *allocator, void *)
{
    uint8_t *zeroes = (uint8_t *)aws_mem_acquire(allocator, sizeof(uint8_t) * 25 * (1 << 20));
    uint32_t res = Aws::Crt::crc32(zeroes, 25 * (1 << 20), 0);
    uint32_t expected = 0x72103906;
    ASSERT_UINT_EQUALS(res, expected);
    aws_mem_release(allocator, zeroes);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(testCrc32LargeBuffer, s_testCrc32LargeBuffer)

static int s_testCrc32CZeroes(struct aws_allocator *allocator, void *)
{
    uint8_t zeroes[] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
    uint32_t res = Aws::Crt::crc32c(zeroes, 32, 0);
    uint32_t expected = 0x8A9136AA;
    ASSERT_UINT_EQUALS(res, expected);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(testCrc32CZeroes, s_testCrc32CZeroes)

static int s_testCrc32CZeroesIterated(struct aws_allocator *allocator, void *)
{
    uint32_t res = 0;
    for (int i = 0; i < 32; i++)
    {
        uint8_t buf[] = {0};
        res = Aws::Crt::crc32c(buf, 1, res);
    }
    uint32_t expected = 0x8A9136AA;
    ASSERT_UINT_EQUALS(res, expected);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(testCrc32CZeroesIterated, s_testCrc32CZeroesIterated)

static int s_testCrc32CValues(struct aws_allocator *allocator, void *)
{
    uint8_t values[] = {0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15,
                        16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31};
    uint32_t res = Aws::Crt::crc32c(values, 32, 0);
    uint32_t expected = 0x46DD794E;
    ASSERT_UINT_EQUALS(res, expected);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(testCrc32CValues, s_testCrc32CValues)

static int s_testCrc32CValuesIterated(struct aws_allocator *allocator, void *)
{
    uint32_t res = 0;
    for (int i = 0; i < 32; i++)
    {
        uint8_t buf[] = {0};
        buf[0] = i;
        res = Aws::Crt::crc32c(buf, 1, res);
    }
    uint32_t expected = 0x46DD794E;
    ASSERT_UINT_EQUALS(res, expected);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(testCrc32CValuesIterated, s_testCrc32CValuesIterated)

static int s_testCrc32CLargeBuffer(struct aws_allocator *allocator, void *)
{
    uint8_t *zeroes = (uint8_t *)aws_mem_acquire(allocator, sizeof(uint8_t) * 25 * (1 << 20));
    uint32_t res = Aws::Crt::crc32c(zeroes, 25 * (1 << 20), 0);
    uint32_t expected = 0xfb5b991d;
    ASSERT_UINT_EQUALS(res, expected);
    aws_mem_release(allocator, zeroes);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(testCrc32CLargeBuffer, s_testCrc32CLargeBuffer)
