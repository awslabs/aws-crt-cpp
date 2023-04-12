/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/crypto/SymmetricCipher.h>
#include <aws/testing/aws_test_harness.h>

#include <utility>

static int s_TestAES_256_CBC_Generated_Materials_ResourceSafety(struct aws_allocator *allocator, void *)
{
    {
        Aws::Crt::ApiHandle apiHandle(allocator);
        Aws::Crt::Crypto::SymmetricCipher cbcCipher = Aws::Crt::Crypto::SymmetricCipher::CreateAES_256_CBC_Cipher(allocator);
        ASSERT_TRUE(cbcCipher);

        auto input = aws_byte_cursor_from_c_str("abc");
       
        uint8_t output[Aws::Crt::Crypto::AES_256_CIPHER_BLOCK_SIZE] = {0};
        auto outputBuf = Aws::Crt::ByteBufFromEmptyArray(output, sizeof(output));

        ASSERT_TRUE(cbcCipher.Encrypt(input, outputBuf));
        ASSERT_TRUE(cbcCipher.FinalizeEncryption(outputBuf));

        ASSERT_FALSE(cbcCipher);

        ASSERT_TRUE(cbcCipher.Reset());

        auto decryptInput = Aws::Crt::ByteCursorFromByteBuf(outputBuf);
        outputBuf.len = 0;

        ASSERT_TRUE(cbcCipher.Decrypt(decryptInput, outputBuf));
        ASSERT_TRUE(cbcCipher.FinalizeDecryption(outputBuf));

        ASSERT_BIN_ARRAYS_EQUALS(input.ptr, input.len, outputBuf.buffer, outputBuf.len);

        ASSERT_FALSE(cbcCipher);

        ASSERT_UINT_EQUALS(Aws::Crt::Crypto::AES_256_KEY_SIZE_BYTES, cbcCipher.GetKey().len);
        ASSERT_UINT_EQUALS(Aws::Crt::Crypto::AES_256_CIPHER_BLOCK_SIZE, cbcCipher.GetIV().len);

        ASSERT_FALSE(cbcCipher);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(AES_256_CBC_Generated_Materials_ResourceSafety, s_TestAES_256_CBC_Generated_Materials_ResourceSafety)

static int s_TestAES_256_CTR_Generated_Materials_ResourceSafety(struct aws_allocator *allocator, void *)
{
    {
        Aws::Crt::ApiHandle apiHandle(allocator);
        Aws::Crt::Crypto::SymmetricCipher ctrCipher =
            Aws::Crt::Crypto::SymmetricCipher::CreateAES_256_CTR_Cipher(allocator);
        ASSERT_TRUE(ctrCipher);

        auto input = aws_byte_cursor_from_c_str("abc");

        uint8_t output[Aws::Crt::Crypto::AES_256_CIPHER_BLOCK_SIZE] = {0};
        auto outputBuf = Aws::Crt::ByteBufFromEmptyArray(output, sizeof(output));

        ASSERT_TRUE(ctrCipher.Encrypt(input, outputBuf));
        ASSERT_TRUE(ctrCipher.FinalizeEncryption(outputBuf));

        ASSERT_FALSE(ctrCipher);

        ASSERT_TRUE(ctrCipher.Reset());

        auto decryptInput = Aws::Crt::ByteCursorFromByteBuf(outputBuf);
        outputBuf.len = 0;

        ASSERT_TRUE(ctrCipher.Decrypt(decryptInput, outputBuf));
        ASSERT_TRUE(ctrCipher.FinalizeDecryption(outputBuf));

        ASSERT_BIN_ARRAYS_EQUALS(input.ptr, input.len, outputBuf.buffer, outputBuf.len);

        ASSERT_FALSE(ctrCipher);

        ASSERT_UINT_EQUALS(Aws::Crt::Crypto::AES_256_KEY_SIZE_BYTES, ctrCipher.GetKey().len);
        ASSERT_UINT_EQUALS(Aws::Crt::Crypto::AES_256_CIPHER_BLOCK_SIZE, ctrCipher.GetIV().len);

        ASSERT_FALSE(ctrCipher);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(AES_256_CTR_Generated_Materials_ResourceSafety, s_TestAES_256_CTR_Generated_Materials_ResourceSafety)

static int s_TestAES_256_GCM_Generated_Materials_ResourceSafety(struct aws_allocator *allocator, void *)
{
    {
        Aws::Crt::ApiHandle apiHandle(allocator);
        Aws::Crt::Crypto::SymmetricCipher gcmCipher =
            Aws::Crt::Crypto::SymmetricCipher::CreateAES_256_GCM_Cipher(allocator);
        ASSERT_TRUE(gcmCipher);

        auto input = aws_byte_cursor_from_c_str("abc");

        uint8_t output[Aws::Crt::Crypto::AES_256_CIPHER_BLOCK_SIZE] = {0};
        auto outputBuf = Aws::Crt::ByteBufFromEmptyArray(output, sizeof(output));

        ASSERT_TRUE(gcmCipher.Encrypt(input, outputBuf));
        ASSERT_TRUE(gcmCipher.FinalizeEncryption(outputBuf));

        ASSERT_FALSE(gcmCipher);

        ASSERT_TRUE(gcmCipher.Reset());

        auto decryptInput = Aws::Crt::ByteCursorFromByteBuf(outputBuf);
        outputBuf.len = 0;

        ASSERT_TRUE(gcmCipher.Decrypt(decryptInput, outputBuf));
        ASSERT_TRUE(gcmCipher.FinalizeDecryption(outputBuf));

        ASSERT_BIN_ARRAYS_EQUALS(input.ptr, input.len, outputBuf.buffer, outputBuf.len);

        ASSERT_FALSE(gcmCipher);

        ASSERT_UINT_EQUALS(Aws::Crt::Crypto::AES_256_KEY_SIZE_BYTES, gcmCipher.GetKey().len);
        ASSERT_UINT_EQUALS(Aws::Crt::Crypto::AES_256_CIPHER_BLOCK_SIZE - 4, gcmCipher.GetIV().len);
        ASSERT_UINT_EQUALS(Aws::Crt::Crypto::AES_256_CIPHER_BLOCK_SIZE, gcmCipher.GetTag().len);

        ASSERT_FALSE(gcmCipher);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(AES_256_GCM_Generated_Materials_ResourceSafety, s_TestAES_256_GCM_Generated_Materials_ResourceSafety)

static int s_TestAES_256_Keywrap_Generated_Materials_ResourceSafety(struct aws_allocator *allocator, void *)
{
    {
        Aws::Crt::ApiHandle apiHandle(allocator);
        Aws::Crt::Crypto::SymmetricCipher keywrapCipher =
            Aws::Crt::Crypto::SymmetricCipher::CreateAES_256_KeyWrap_Cipher(allocator);
        ASSERT_TRUE(keywrapCipher);

        auto input = aws_byte_cursor_from_c_str("abcdefghijklmnopqrstuvwxyz123456");

        uint8_t output[Aws::Crt::Crypto::AES_256_CIPHER_BLOCK_SIZE * 3] = {0};
        auto outputBuf = Aws::Crt::ByteBufFromEmptyArray(output, sizeof(output));

        ASSERT_TRUE(keywrapCipher.Encrypt(input, outputBuf));
        ASSERT_TRUE(keywrapCipher.FinalizeEncryption(outputBuf));

        ASSERT_FALSE(keywrapCipher);

        ASSERT_TRUE(keywrapCipher.Reset());

        auto decryptInput = Aws::Crt::ByteCursorFromByteBuf(outputBuf);
        outputBuf.len = 0;

        ASSERT_TRUE(keywrapCipher.Decrypt(decryptInput, outputBuf));
        ASSERT_TRUE(keywrapCipher.FinalizeDecryption(outputBuf));

        ASSERT_BIN_ARRAYS_EQUALS(input.ptr, input.len, outputBuf.buffer, outputBuf.len);

        ASSERT_FALSE(keywrapCipher);

        ASSERT_UINT_EQUALS(Aws::Crt::Crypto::AES_256_KEY_SIZE_BYTES, keywrapCipher.GetKey().len);
        ASSERT_UINT_EQUALS(0u, keywrapCipher.GetIV().len);

        ASSERT_FALSE(keywrapCipher);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(AES_256_Keywrap_Generated_Materials_ResourceSafety, s_TestAES_256_Keywrap_Generated_Materials_ResourceSafety)