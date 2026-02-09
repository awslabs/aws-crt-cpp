#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>

struct aws_xxhash;
namespace Aws
{
    namespace Crt
    {
        namespace Checksum
        {
            /**
             * Computes a XXHash64 Hash over input, and writes the digest to output.
             * Returns true on success. If this function fails,
             * Aws::Crt::LastError() will contain the error that occurred.
             */
            bool AWS_CRT_CPP_API ComputeXXHash64(const ByteCursor &input, ByteBuf &output, uint64_t seed = 0) noexcept;

            /**
             * Computes a XXHash3_64 Hash using the default allocator over input, and writes the digest to output.
             * Returns true on success. If this function fails,
             * Aws::Crt::LastError() will contain the error that occurred.
             */
            bool AWS_CRT_CPP_API
                ComputeXXHash3_64(const ByteCursor &input, ByteBuf &output, uint64_t seed = 0) noexcept;

            /**
             * Computes a XXHash3_128 Hash using the default allocator over input, and writes the digest to output.
             * Returns true on success. If this function fails,
             * Aws::Crt::LastError() will contain the error that occurred.
             */
            bool AWS_CRT_CPP_API
                ComputeXXHash3_128(const ByteCursor &input, ByteBuf &output, uint64_t seed = 0) noexcept;

            /**
             * Streaming Hash object. The typical use case is for computing the hash of an object that is too large to
             * load into memory. You can call Update() multiple times as you load chunks of data into memory. When
             * you're finished simply call Digest(). After Digest() is called, this object is no longer usable.
             */
            class AWS_CRT_CPP_API XXHash final
            {
              public:
                ~XXHash();
                XXHash(const XXHash &) = delete;
                XXHash &operator=(const XXHash &) = delete;
                XXHash(XXHash &&toMove);
                XXHash &operator=(XXHash &&toMove);

                /**
                 * Returns the value of the last aws error encountered by operations on this instance.
                 */
                inline int LastError() const noexcept { return m_lastError; }

                /**
                 * Creates an instance of a Streaming XXHash64 Hash.
                 */
                static XXHash CreateXXHash64(uint64_t seed = 0, Allocator *allocator = ApiAllocator()) noexcept;

                /**
                 * Creates an instance of a Streaming XXHash3_64 Hash.
                 */
                static XXHash CreateXXHash3_64(uint64_t seed = 0, Allocator *allocator = ApiAllocator()) noexcept;

                /**
                 * Creates an instance of a Streaming XXHash3_128 Hash.
                 */
                static XXHash CreateXXHash3_128(uint64_t seed = 0, Allocator *allocator = ApiAllocator()) noexcept;

                /**
                 * Updates the running hash object with data in toHash. Returns true on success. Call
                 * LastError() for the reason this call failed.
                 */
                bool Update(const ByteCursor &toHash) noexcept;

                /**
                 * Finishes the running hash operation and writes the digest into output.
                 * Returns true on success. Call LastError() for the reason this
                 * call failed.
                 */
                bool Digest(ByteBuf &output) noexcept;

              private:
                XXHash(aws_xxhash *hash) noexcept;
                XXHash() = delete;

                aws_xxhash *m_hash;
                int m_lastError;
            };
        } // namespace Checksum
    } // namespace Crt
} // namespace Aws
