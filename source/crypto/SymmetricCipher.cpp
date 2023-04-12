/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/crypto/SymmetricCipher.h>

#include <aws/cal/symmetric_cipher.h>

namespace Aws
{
    namespace Crt
    {
        namespace Crypto
        {
            SymmetricCipher::SymmetricCipher(aws_symmetric_cipher *cipher) noexcept : m_cipher(cipher), m_lastError(0)
            {
                if (cipher == nullptr)
                {
                    m_lastError = Crt::LastError();
                }
            }

            SymmetricCipher ::~SymmetricCipher()
            {
                if (m_cipher)
                {
                    aws_symmetric_cipher_destroy(m_cipher);
                    m_cipher = nullptr;
                }
            }

            SymmetricCipher::SymmetricCipher(SymmetricCipher &&toMove) noexcept
                : m_cipher(toMove.m_cipher), m_lastError(toMove.m_lastError)
            {
                toMove.m_cipher = nullptr;
            }

            SymmetricCipher &SymmetricCipher::operator=(SymmetricCipher &&toMove) noexcept
            {
                if (this != &toMove)
                {
                    m_cipher = toMove.m_cipher;
                    m_lastError = toMove.m_lastError;
                    toMove.m_cipher = nullptr;
                }

                return *this;
            }

            SymmetricCipher::operator bool() const noexcept
            {
                return m_cipher != nullptr ? aws_symmetric_cipher_is_good(m_cipher) : false;
            }

            bool SymmetricCipher::Encrypt(const ByteCursor &toEncrypt, ByteBuf &out) noexcept
            {
                if (aws_symmetric_cipher_encrypt(m_cipher, toEncrypt, &out) != AWS_OP_SUCCESS)
                {
                    m_lastError = Aws::Crt::LastError();
                    return false;
                }

                return true;
            }

            bool SymmetricCipher::FinalizeEncryption(ByteBuf &out) noexcept
            {
                if (aws_symmetric_cipher_finalize_encryption(m_cipher, &out) != AWS_OP_SUCCESS)
                {
                    m_lastError = Aws::Crt::LastError();
                    return false;
                }

                return true;
            }

            bool SymmetricCipher::Decrypt(const ByteCursor &toDecrypt, ByteBuf &out) noexcept
            {
                if (aws_symmetric_cipher_decrypt(m_cipher, toDecrypt, &out) != AWS_OP_SUCCESS)
                {
                    m_lastError = Aws::Crt::LastError();
                    return false;
                }

                return true;
            }

            bool SymmetricCipher::FinalizeDecryption(ByteBuf &out) noexcept
            {
                if (aws_symmetric_cipher_finalize_decryption(m_cipher, &out) != AWS_OP_SUCCESS)
                {
                    m_lastError = Aws::Crt::LastError();
                    return false;
                }

                return true;
            }

            bool SymmetricCipher::Reset() noexcept
            {
                if (aws_symmetric_cipher_reset(m_cipher) != AWS_OP_SUCCESS)
                {
                    m_lastError = Aws::Crt::LastError();
                    return false;
                }

                m_lastError = 0;

                return true;
            }

            const ByteCursor SymmetricCipher::GetKey() const noexcept { return aws_symmetric_cipher_get_key(m_cipher); }

            const ByteCursor SymmetricCipher::GetIV() const noexcept
            {
                return aws_symmetric_cipher_get_initialization_vector(m_cipher);
            }

            const ByteCursor SymmetricCipher::GetTag() const noexcept { return aws_symmetric_cipher_get_tag(m_cipher); }

            SymmetricCipher SymmetricCipher::CreateAES_256_CBC_Cipher(Allocator *allocator) noexcept
            {
                return SymmetricCipher(aws_aes_cbc_256_new(allocator, nullptr, nullptr));
            }

            SymmetricCipher SymmetricCipher::CreateAES_256_CBC_Cipher(
                const ByteCursor &key,
                const ByteCursor &iv,
                Allocator *allocator) noexcept
            {
                return SymmetricCipher(aws_aes_cbc_256_new(allocator, &key, &iv));
            }

            SymmetricCipher SymmetricCipher::CreateAES_256_CTR_Cipher(Allocator *allocator) noexcept
            {
                return SymmetricCipher(aws_aes_ctr_256_new(allocator, nullptr, nullptr));
            }

            SymmetricCipher SymmetricCipher::CreateAES_256_CTR_Cipher(
                const ByteCursor &key,
                const ByteCursor &iv,
                Allocator *allocator) noexcept
            {
                return SymmetricCipher(aws_aes_ctr_256_new(allocator, &key, &iv));
            }

            SymmetricCipher SymmetricCipher::CreateAES_256_GCM_Cipher(Allocator *allocator) noexcept
            {
                return SymmetricCipher(aws_aes_gcm_256_new(allocator, nullptr, nullptr, nullptr, nullptr));
            }

            SymmetricCipher SymmetricCipher::CreateAES_256_GCM_Cipher(
                const ByteCursor &key,
                const ByteCursor &iv,
                const Optional<ByteCursor> &tag,
                const Optional<ByteCursor> &aad,
                Allocator *allocator) noexcept
            {
                return SymmetricCipher(aws_aes_gcm_256_new(
                    allocator,
                    &key,
                    &iv,
                    tag.has_value() ? &tag.value() : nullptr,
                    aad.has_value() ? &aad.value() : nullptr));
            }

            SymmetricCipher SymmetricCipher::CreateAES_256_KeyWrap_Cipher(Allocator *allocator) noexcept
            {
                return SymmetricCipher(aws_aes_keywrap_256_new(allocator, nullptr));
            }

            SymmetricCipher SymmetricCipher::CreateAES_256_KeyWrap_Cipher(
                const ByteCursor &key,
                Allocator *allocator) noexcept
            {
                return SymmetricCipher(aws_aes_keywrap_256_new(allocator, &key));
            }
        } // namespace Crypto
    }     // namespace Crt
} // namespace Aws
