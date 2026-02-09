/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/checksum/XXHash.h>

namespace Aws
{
    namespace Crt
    {
        namespace Checksum
        {
            bool ComputeXXhash64(const ByteCursor &input, ByteBuf &output, uint64_t seed = 0) noexcept
            {
                return aws_xxhash64_compute(seed, input, &output) == AWS_OP_SUCCESS;
            }

            bool ComputeXXhash3_64(const ByteCursor &input, ByteBuf &output, uint64_t seed = 0) noexcept
            {
                return aws_xxhash3_64_compute(seed, input, &output) == AWS_OP_SUCCESS;
            }

            bool ComputeXXhash3_128(const ByteCursor &input, ByteBuf &output, uint64_t seed = 0) noexcept
            {
                return aws_xxhash3_128_compute(seed, input, &output) == AWS_OP_SUCCESS;
            }

            XXHash::XXHash(aws_xxhash *hash) noexcept : m_hash(hash), m_lastError(0)
            {
                if (!hash)
                {
                    m_lastError = aws_last_error();
                }
            }

            XXHash::~XXHash()
            {
                if (m_hash)
                {
                    aws_xxhash_destroy(m_hash);
                    m_hash = nullptr;
                }
            }

            XXHash::XXHash(XXHash &&toMove) : m_hash(toMove.m_hash), m_lastError(toMove.m_lastError)
            {
                toMove.m_hash = nullptr;
            }

            XXHash &XXHash::operator=(XXHash &&toMove)
            {
                if (&toMove != this)
                {
                    *this = XXHash(std::move(toMove));
                }

                return *this;
            }

            XXHash XXHash::CreateXXHash64(uint64_t seed = 0, Allocator *allocator) noexcept
            {
                return XXHash(aws_xxhash64_new(allocator, seed));
            }

            XXHash XXHash::CreateXXHash3_64(uint64_t seed = 0, Allocator *allocator) noexcept
            {
                return XXHash(aws_xxhash3_64_new(allocator, seed));
            }

            XXHash XXHash::CreateXXHash3_128(uint64_t seed = 0, Allocator *allocator) noexcept
            {
                return XXHash(aws_xxhash3_128_new(allocator, seed));
            }

            bool XXHash::Update(const ByteCursor &toHash) noexcept
            {
                if (AWS_OP_SUCCESS != aws_xxhash_update(m_hash, toHash))
                {
                    m_lastError = aws_last_error();
                    return false;
                }
                return true;
            }

            bool XXHash::Digest(ByteBuf &output) noexcept
            {
                if (aws_xxhash_finalize(m_hash, &output) != AWS_OP_SUCCESS)
                {
                    m_lastError = aws_last_error();
                    return false;
                }
                return true;
            }
        } // namespace Checksum
    } // namespace Crt
} // namespace Aws
