#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>

#include <aws/cal/hash.h>

struct aws_hash;
namespace Aws
{
    namespace Crt
    {
        namespace Crypto
        {
            static const size_t SHA256_DIGEST_SIZE = 32;
            static const size_t MD5_DIGEST_SIZE = 16;

            /**
             * Computes a SHA256 Hash over input, and writes the digest to output. If truncateTo is non-zero, the digest
             * will be truncated to the value of truncateTo. Returns true on success. If this function fails,
             * Aws::Crt::LastError() will contain the error that occurred. Unless you're using 'truncateTo', output
             * should have a minimum capacity of SHA256_DIGEST_SIZE.
             */
            bool AWS_CRT_CPP_API ComputeSHA256(
                Allocator *allocator,
                const ByteCursor &input,
                ByteBuf &output,
                size_t truncateTo = 0) noexcept;

            /**
             * Computes a SHA256 Hash using the default allocator over input, and writes the digest to output. If
             * truncateTo is non-zero, the digest will be truncated to the value of truncateTo. Returns true on success.
             * If this function fails, Aws::Crt::LastError() will contain the error that occurred. Unless you're using
             * 'truncateTo', output should have a minimum capacity of SHA256_DIGEST_SIZE.
             */
            bool AWS_CRT_CPP_API
                ComputeSHA256(const ByteCursor &input, ByteBuf &output, size_t truncateTo = 0) noexcept;

            /**
             * Computes a MD5 Hash over input, and writes the digest to output. If truncateTo is non-zero, the digest
             * will be truncated to the value of truncateTo. Returns true on success. If this function fails,
             * Aws::Crt::LastError() will contain the error that occurred. Unless you're using 'truncateTo',
             * output should have a minimum capacity of MD5_DIGEST_SIZE.
             */
            bool AWS_CRT_CPP_API ComputeMD5(
                Allocator *allocator,
                const ByteCursor &input,
                ByteBuf &output,
                size_t truncateTo = 0) noexcept;

            /**
             * Computes a MD5 Hash using the default allocator over input, and writes the digest to output. If
             * truncateTo is non-zero, the digest will be truncated to the value of truncateTo. Returns true on success.
             * If this function fails, Aws::Crt::LastError() will contain the error that occurred. Unless you're using
             * 'truncateTo', output should have a minimum capacity of MD5_DIGEST_SIZE.
             */
            bool AWS_CRT_CPP_API ComputeMD5(const ByteCursor &input, ByteBuf &output, size_t truncateTo = 0) noexcept;

            /**
             * Streaming Hash object. The typical use case is for computing the hash of an object that is too large to
             * load into memory. You can call Update() multiple times as you load chunks of data into memory. When
             * you're finished simply call Digest(). After Digest() is called, this object is no longer usable.
             */
            class AWS_CRT_CPP_API Hash
            {
              public:
                virtual ~Hash();
                Hash(const Hash &) = delete;
                Hash &operator=(const Hash &) = delete;
                Hash(Hash &&toMove);
                Hash &operator=(Hash &&toMove);

                /**
                 * Returns true if the instance is in a valid state, false otherwise.
                 */
                inline operator bool() const noexcept { return m_good; }

                /**
                 * Returns the value of the last aws error encountered by operations on this instance.
                 */
                inline int LastError() const noexcept { return m_lastError; }

                /**
                 * Creates an instance of a Streaming SHA256 Hash.
                 */
                static Hash CreateSHA256(Allocator *allocator = g_allocator) noexcept;

                /**
                 * Creates an instance of a Streaming MD5 Hash.
                 */
                static Hash CreateMD5(Allocator *allocator = g_allocator) noexcept;

                /**
                 * Updates the running hash object with data in toHash. Returns true on success. Call
                 * LastError() for the reason this call failed.
                 */
                bool Update(const ByteCursor &toHash) noexcept;

                /**
                 * Finishes the running hash operation and writes the digest into output. The available capacity of
                 * output must be large enough for the digest. See: SHA256_DIGEST_SIZE and MD5_DIGEST_SIZE for size
                 * hints. 'truncateTo' is for if you want truncated output (e.g. you only want the first 16 bytes of a
                 * SHA256 digest. Returns true on success. Call LastError() for the reason this call failed.
                 */
                bool Digest(ByteBuf &output, size_t truncateTo = 0) noexcept;

                aws_hash *GetUnderlyingHandle() const { return m_hash; }

              protected:
                Hash(aws_hash *hash) noexcept;
                Hash() = delete;

              private:
                aws_hash *m_hash;
                bool m_good;
                int m_lastError;
            };

#ifdef BYO_CRYPTO
            class AWS_CRT_CPP_API ByoHash : public Hash, public std::enable_shared_from_this<ByoHash>
            {
              public:
                virtual ~ByoHash() = default;

              protected:
                ByoHash(size_t digestSize, Allocator *allocator = g_allocator);

                virtual bool UpdateInternal(const ByteCursor &toHash) noexcept = 0;
                virtual bool DigestInternal(ByteBuf &output, size_t truncateTo = 0) noexcept = 0;

              private:
                static void s_Destroy(struct aws_hash *hash);
                static int s_Update(struct aws_hash *hash, const struct aws_byte_cursor *buf);
                static int s_Finalize(struct aws_hash *hash, struct aws_byte_buf *out);

                static aws_hash_vtable s_Vtable;
                aws_hash m_hashValue;
                std::shared_ptr<ByoHash> m_selfReference;
            };

            using CreateHashCallback = std::function<std::shared_ptr<ByoHash>(size_t, Allocator *)>;
#endif /* BYO_CRYPTO */

        } // namespace Crypto
    }     // namespace Crt
} // namespace Aws
