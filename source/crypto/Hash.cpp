/*
 * Copyright 2010-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
#include <aws/crt/crypto/Hash.h>

#include <aws/cal/hash.h>
#include <aws/crt/ByteBuf.h>

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
                return aws_sha256_compute(allocator, input.GetImpl(), output.GetImpl(), truncateTo) == AWS_OP_SUCCESS;
            }

            bool ComputeSHA256(const ByteCursor &input, ByteBuf &output, size_t truncateTo) noexcept
            {
                return aws_sha256_compute(DefaultAllocator(), input.GetImpl(), output.GetImpl(), truncateTo) ==
                       AWS_OP_SUCCESS;
            }

            bool ComputeMD5(Allocator *allocator, const ByteCursor &input, ByteBuf &output, size_t truncateTo) noexcept
            {
                return aws_md5_compute(allocator, input.GetImpl(), output.GetImpl(), truncateTo) == AWS_OP_SUCCESS;
            }

            bool ComputeMD5(const ByteCursor &input, ByteBuf &output, size_t truncateTo) noexcept
            {
                return aws_md5_compute(DefaultAllocator(), input.GetImpl(), output.GetImpl(), truncateTo) ==
                       AWS_OP_SUCCESS;
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
                    if (aws_hash_update(m_hash, toHash.GetImpl()))
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
                    if (aws_hash_finalize(m_hash, output.GetImpl(), truncateTo))
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
