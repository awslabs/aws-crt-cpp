/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/crypto/HMAC.h>

#include <aws/cal/hmac.h>

namespace Aws
{
    namespace Crt
    {
        namespace Crypto
        {
            bool ComputeSHA256HMAC(
                Allocator *allocator,
                const ByteCursor &secret,
                const ByteCursor &input,
                ByteBuf &output,
                size_t truncateTo) noexcept
            {
                return aws_sha256_hmac_compute(allocator, &secret, &input, &output, truncateTo) == AWS_OP_SUCCESS;
            }

            bool ComputeSHA256HMAC(
                const ByteCursor &secret,
                const ByteCursor &input,
                ByteBuf &output,
                size_t truncateTo) noexcept
            {
                return aws_sha256_hmac_compute(g_allocator, &secret, &input, &output, truncateTo) == AWS_OP_SUCCESS;
            }

            HMAC::HMAC(aws_hmac *hmac) noexcept : m_hmac(hmac), m_good(false), m_lastError(0)
            {
                if (hmac)
                {
                    m_good = true;
                }
                else
                {
                    m_lastError = aws_last_error();
                }
            }

            HMAC::~HMAC()
            {
                if (m_hmac)
                {
                    aws_hmac_destroy(m_hmac);
                    m_hmac = nullptr;
                }
            }

            HMAC::HMAC(HMAC &&toMove) : m_hmac(toMove.m_hmac), m_good(toMove.m_good), m_lastError(toMove.m_lastError)
            {
                toMove.m_hmac = nullptr;
                toMove.m_good = false;
            }

            HMAC &HMAC::operator=(HMAC &&toMove)
            {
                if (&toMove != this)
                {
                    *this = HMAC(std::move(toMove));
                }

                return *this;
            }

            HMAC HMAC::CreateSHA256HMAC(Allocator *allocator, const ByteCursor &secret) noexcept
            {
                return HMAC(aws_sha256_hmac_new(allocator, &secret));
            }

            HMAC HMAC::CreateSHA256HMAC(const ByteCursor &secret) noexcept
            {
                return HMAC(aws_sha256_hmac_new(g_allocator, &secret));
            }

            bool HMAC::Update(const ByteCursor &toHMAC) noexcept
            {
                if (*this)
                {
                    if (aws_hmac_update(m_hmac, &toHMAC))
                    {
                        m_lastError = aws_last_error();
                        m_good = false;
                        return false;
                    }
                    return true;
                }

                return false;
            }

            bool HMAC::Digest(ByteBuf &output, size_t truncateTo) noexcept
            {
                if (*this)
                {
                    m_good = false;
                    if (aws_hmac_finalize(m_hmac, &output, truncateTo))
                    {
                        m_lastError = aws_last_error();
                        return false;
                    }
                    return true;
                }

                return false;
            }

        } // namespace Crypto
    }     // namespace Crt
} // namespace Aws
