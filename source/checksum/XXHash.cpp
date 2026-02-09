/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/checksums/xxhash.h>
#include <aws/crt/checksum/XXHash.h>

namespace Aws
{
    namespace Crt
    {
        namespace Checksum
        {
            bool ComputeXXHash64(const ByteCursor &input, ByteBuf &output, uint64_t seed) noexcept
            {
                return aws_xxhash64_compute(seed, input, &output) == AWS_OP_SUCCESS;
            }

            bool ComputeXXHash3_64(const ByteCursor &input, ByteBuf &output, uint64_t seed) noexcept
            {
                return aws_xxhash3_64_compute(seed, input, &output) == AWS_OP_SUCCESS;
            }

            bool ComputeXXHash3_128(const ByteCursor &input, ByteBuf &output, uint64_t seed) noexcept
            {
                return aws_xxhash3_128_compute(seed, input, &output) == AWS_OP_SUCCESS;
            }

            XXHash::XXHash(aws_xxhash *hash) noexcept : m_hash(hash, aws_xxhash_destroy), m_lastError(0)
            {
                if (hash == nullptr)
                {
                    m_lastError = Crt::LastError();
                }
            }

            XXHash XXHash::CreateXXHash64(uint64_t seed, Allocator *allocator) noexcept
            {
                return XXHash(aws_xxhash64_new(allocator, seed));
            }

            XXHash XXHash::CreateXXHash3_64(uint64_t seed, Allocator *allocator) noexcept
            {
                return XXHash(aws_xxhash3_64_new(allocator, seed));
            }

            XXHash XXHash::CreateXXHash3_128(uint64_t seed, Allocator *allocator) noexcept
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
