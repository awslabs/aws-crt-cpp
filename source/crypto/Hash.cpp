/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/crypto/Hash.h>

#include <aws/cal/hash.h>

namespace Aws
{
    namespace Crt
    {
        namespace Crypto
        {
            bool ComputeSHA256(
                Allocator *allocator,
                const ByteCursor &input,
                ByteBuf &output,
                size_t truncateTo) noexcept
            {
                return aws_sha256_compute(allocator, &input, &output, truncateTo) == AWS_OP_SUCCESS;
            }

            bool ComputeSHA256(const ByteCursor &input, ByteBuf &output, size_t truncateTo) noexcept
            {
                return aws_sha256_compute(DefaultAllocator(), &input, &output, truncateTo) == AWS_OP_SUCCESS;
            }

            bool ComputeMD5(Allocator *allocator, const ByteCursor &input, ByteBuf &output, size_t truncateTo) noexcept
            {
                return aws_md5_compute(allocator, &input, &output, truncateTo) == AWS_OP_SUCCESS;
            }

            bool ComputeMD5(const ByteCursor &input, ByteBuf &output, size_t truncateTo) noexcept
            {
                return aws_md5_compute(DefaultAllocator(), &input, &output, truncateTo) == AWS_OP_SUCCESS;
            }

            Hash::Hash(aws_hash *hash) noexcept : m_hash(hash), m_good(false), m_lastError(0)
            {
                if (hash)
                {
                    m_good = true;
                }
                else
                {
                    m_lastError = aws_last_error();
                }
            }

            Hash::~Hash()
            {
                if (m_hash)
                {
                    aws_hash_destroy(m_hash);
                    m_hash = nullptr;
                }
            }

            Hash::Hash(Hash &&toMove) : m_hash(toMove.m_hash), m_good(toMove.m_good), m_lastError(toMove.m_lastError)
            {
                toMove.m_hash = nullptr;
                toMove.m_good = false;
            }

            Hash &Hash::operator=(Hash &&toMove)
            {
                if (&toMove != this)
                {
                    *this = Hash(std::move(toMove));
                }

                return *this;
            }

            Hash Hash::CreateSHA256(Allocator *allocator) noexcept { return Hash(aws_sha256_new(allocator)); }

            Hash Hash::CreateMD5(Allocator *allocator) noexcept { return Hash(aws_md5_new(allocator)); }

            bool Hash::Update(const ByteCursor &toHash) noexcept
            {
                if (*this)
                {
                    if (aws_hash_update(m_hash, &toHash))
                    {
                        m_lastError = aws_last_error();
                        m_good = false;
                        return false;
                    }
                    return true;
                }

                return false;
            }

            bool Hash::Digest(ByteBuf &output, size_t truncateTo) noexcept
            {
                if (*this)
                {
                    m_good = false;
                    if (aws_hash_finalize(m_hash, &output, truncateTo))
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
