/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/checksum/XXHash.h>
#include <aws/testing/aws_test_harness.h>

static int s_TestXXHash64Piping(struct aws_allocator *allocator, void *)
{
    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::ByteCursor dataCur = aws_byte_cursor_from_c_str("Hello world");

    uint8_t expected[] = {0xc5, 0x00, 0xb0, 0xc9, 0x12, 0xb3, 0x76, 0xd8};

    Aws::Crt::ByteBuf result;
    aws_byte_buf_init(&result, allocator, 8);

    ASSERT_TRUE(Aws::Crt::Checksum::ComputeXXHash64(dataCur, result));

    ASSERT_BIN_ARRAYS_EQUALS(result.buffer, result.len, expected, sizeof(expected));

    aws_byte_buf_reset(&result, false);

    auto hash = Aws::Crt::Checksum::XXHash::CreateXXHash64(0, allocator);
    ASSERT_TRUE(hash.Update(dataCur));
    ASSERT_TRUE(hash.Digest(result));

    ASSERT_BIN_ARRAYS_EQUALS(result.buffer, result.len, expected, sizeof(expected));

    aws_byte_buf_clean_up(&result);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(XXHash64Piping, s_TestXXHash64Piping)

static int s_TestXXHash3_64Piping(struct aws_allocator *allocator, void *)
{
    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::ByteCursor dataCur = aws_byte_cursor_from_c_str("Hello world");

    uint8_t expected[] = {0xb6, 0xac, 0xb9, 0xd8, 0x4a, 0x38, 0xff, 0x74};

    Aws::Crt::ByteBuf result;
    aws_byte_buf_init(&result, allocator, 8);

    ASSERT_TRUE(Aws::Crt::Checksum::ComputeXXHash3_64(dataCur, result));

    ASSERT_BIN_ARRAYS_EQUALS(result.buffer, result.len, expected, sizeof(expected));

    aws_byte_buf_reset(&result, false);

    auto hash = Aws::Crt::Checksum::XXHash::CreateXXHash3_64(0, allocator);
    ASSERT_TRUE(hash.Update(dataCur));
    ASSERT_TRUE(hash.Digest(result));

    ASSERT_BIN_ARRAYS_EQUALS(result.buffer, result.len, expected, sizeof(expected));

    aws_byte_buf_clean_up(&result);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(XXHash3_64Piping, s_TestXXHash3_64Piping)

static int s_TestXXHash3_128Piping(struct aws_allocator *allocator, void *)
{
    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::ByteCursor dataCur = aws_byte_cursor_from_c_str("Hello world");

    uint8_t expected[] = {
        0x73, 0x51, 0xf8, 0x98, 0x12, 0xf9, 0x73, 0x82, 0xb9, 0x1d, 0x05, 0xb3, 0x1e, 0x04, 0xdd, 0x7f};

    Aws::Crt::ByteBuf result;
    aws_byte_buf_init(&result, allocator, 16);

    ASSERT_TRUE(Aws::Crt::Checksum::ComputeXXHash3_128(dataCur, result));

    ASSERT_BIN_ARRAYS_EQUALS(result.buffer, result.len, expected, sizeof(expected));

    aws_byte_buf_reset(&result, false);

    auto hash = Aws::Crt::Checksum::XXHash::CreateXXHash3_128(0, allocator);
    ASSERT_TRUE(hash.Update(dataCur));
    ASSERT_TRUE(hash.Digest(result));

    ASSERT_BIN_ARRAYS_EQUALS(result.buffer, result.len, expected, sizeof(expected));

    aws_byte_buf_clean_up(&result);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(XXHash3_128Piping, s_TestXXHash3_128Piping)
