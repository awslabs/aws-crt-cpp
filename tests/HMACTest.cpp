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
#include <aws/crt/crypto/HMAC.h>
#include <aws/testing/aws_test_harness.h>

static int s_TestSHA256HMACResourceSafety(struct aws_allocator *allocator, void *)
{
    Aws::Crt::ApiHandle apiHandle(allocator);
    uint8_t secret[] = {
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
        0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b, 0x0b,
    };

    Aws::Crt::ByteCursor secretCur(secret, sizeof(secret));

    Aws::Crt::Crypto::HMAC sha256Hmac = Aws::Crt::Crypto::HMAC::CreateSHA256HMAC(allocator, secretCur);
    ASSERT_TRUE(sha256Hmac);

    Aws::Crt::ByteCursor input("Hi There");
    uint8_t expected[] = {
        0xb0, 0x34, 0x4c, 0x61, 0xd8, 0xdb, 0x38, 0x53, 0x5c, 0xa8, 0xaf, 0xce, 0xaf, 0x0b, 0xf1, 0x2b,
        0x88, 0x1d, 0xc2, 0x00, 0xc9, 0x83, 0x3d, 0xa7, 0x26, 0xe9, 0x37, 0x6c, 0x2e, 0x32, 0xcf, 0xf7,
    };
    Aws::Crt::ByteBuf expectedBuf(expected, sizeof(expected), sizeof(expected));

    uint8_t output[Aws::Crt::Crypto::SHA256_HMAC_DIGEST_SIZE] = {0};
    Aws::Crt::ByteBuf outputBuf(output, sizeof(output), 0);

    ASSERT_TRUE(sha256Hmac.Update(input));
    ASSERT_TRUE(sha256Hmac.Digest(outputBuf));
    ASSERT_FALSE(sha256Hmac);

    ASSERT_BIN_ARRAYS_EQUALS(
        expectedBuf.GetImpl()->buffer,
        expectedBuf.GetImpl()->len,
        outputBuf.GetImpl()->buffer,
        outputBuf.GetImpl()->len);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(SHA256HMACResourceSafety, s_TestSHA256HMACResourceSafety)
