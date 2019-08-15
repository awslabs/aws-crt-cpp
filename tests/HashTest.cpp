/*
 * Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
#include <aws/crt/Api.h>
#include <aws/crt/ByteBuf.h>
#include <aws/crt/crypto/Hash.h>
#include <aws/testing/aws_test_harness.h>

static int s_TestSHA256ResourceSafety(struct aws_allocator *allocator, void *)
{
    Aws::Crt::ApiHandle apiHandle(allocator);
    Aws::Crt::Crypto::Hash sha256 = Aws::Crt::Crypto::Hash::CreateSHA256(allocator);
    ASSERT_TRUE(sha256);

    Aws::Crt::ByteCursor input("abc");
    uint8_t expected[] = {
        0xba, 0x78, 0x16, 0xbf, 0x8f, 0x01, 0xcf, 0xea, 0x41, 0x41, 0x40, 0xde, 0x5d, 0xae, 0x22, 0x23,
        0xb0, 0x03, 0x61, 0xa3, 0x96, 0x17, 0x7a, 0x9c, 0xb4, 0x10, 0xff, 0x61, 0xf2, 0x00, 0x15, 0xad,
    };
    Aws::Crt::ByteBuf expectedBuf(expected, sizeof(expected), sizeof(expected));

    uint8_t output[Aws::Crt::Crypto::SHA256_DIGEST_SIZE] = {0};
    Aws::Crt::ByteBuf outputBuf(output, sizeof(output), 0);

    ASSERT_TRUE(sha256.Update(input));
    ASSERT_TRUE(sha256.Digest(outputBuf));
    ASSERT_FALSE(sha256);

    ASSERT_BIN_ARRAYS_EQUALS(
        expectedBuf.GetImpl()->buffer,
        expectedBuf.GetImpl()->len,
        outputBuf.GetImpl()->buffer,
        outputBuf.GetImpl()->len);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(SHA256ResourceSafety, s_TestSHA256ResourceSafety)

static int s_TestMD5ResourceSafety(struct aws_allocator *allocator, void *)
{
    Aws::Crt::ApiHandle apiHandle(allocator);
    Aws::Crt::Crypto::Hash md5 = Aws::Crt::Crypto::Hash::CreateMD5(allocator);
    ASSERT_TRUE(md5);

    Aws::Crt::ByteCursor input("abc");
    uint8_t expected[] = {
        0x90,
        0x01,
        0x50,
        0x98,
        0x3c,
        0xd2,
        0x4f,
        0xb0,
        0xd6,
        0x96,
        0x3f,
        0x7d,
        0x28,
        0xe1,
        0x7f,
        0x72,
    };
    Aws::Crt::ByteBuf expectedBuf(expected, sizeof(expected), sizeof(expected));

    uint8_t output[Aws::Crt::Crypto::MD5_DIGEST_SIZE] = {0};
    Aws::Crt::ByteBuf outputBuf(output, sizeof(output), 0);

    ASSERT_TRUE(md5.Update(input));
    ASSERT_TRUE(md5.Digest(outputBuf));
    ASSERT_FALSE(md5);

    ASSERT_BIN_ARRAYS_EQUALS(
        expectedBuf.GetImpl()->buffer,
        expectedBuf.GetImpl()->len,
        outputBuf.GetImpl()->buffer,
        outputBuf.GetImpl()->len);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(MD5ResourceSafety, s_TestMD5ResourceSafety)
