#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/cal/hmac.h>
#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>

struct aws_hmac;
namespace Aws
{
    namespace Crt
    {
        namespace Crypto
        {
            static const size_t SHA256_HMAC_DIGEST_SIZE = 32;

            /**
             * Computes a SHA256 HMAC with secret over input, and writes the digest to output. If truncateTo is
             * non-zero, the digest will be truncated to the value of truncateTo. Returns true on success. If this
             * function fails, Aws::Crt::LastError() will contain the error that occurred. Unless you're using
             * 'truncateTo', output should have a minimum capacity of SHA256_HMAC_DIGEST_SIZE.
             */
            bool AWS_CRT_CPP_API ComputeSHA256HMAC(
                Allocator *allocator,
                const ByteCursor &secret,
                const ByteCursor &input,
                ByteBuf &output,
                size_t truncateTo = 0) noexcept;

            /**
             * Computes a SHA256 HMAC using the default allocator with secret over input, and writes the digest to
             * output. If truncateTo is non-zero, the digest will be truncated to the value of truncateTo. Returns true
             * on success. If this function fails, Aws::Crt::LastError() will contain the error that occurred. Unless
             * you're using 'truncateTo', output should have a minimum capacity of SHA256_HMAC_DIGEST_SIZE.
             */
            bool AWS_CRT_CPP_API ComputeSHA256HMAC(
                const ByteCursor &secret,
                const ByteCursor &input,
                ByteBuf &output,
                size_t truncateTo = 0) noexcept;
            /**
             * Streaming HMAC object. The typical use case is for computing the HMAC of an object that is too large to
             * load into memory. You can call Update() multiple times as you load chunks of data into memory. When
             * you're finished simply call Digest(). After Digest() is called, this object is no longer usable.
             */
            class AWS_CRT_CPP_API HMAC
            {
              public:
                ~HMAC();
                HMAC(const HMAC &) = delete;
                HMAC &operator=(const HMAC &) = delete;
                HMAC(HMAC &&toMove);
                HMAC &operator=(HMAC &&toMove);

                /**
                 * Returns true if the instance is in a valid state, false otherwise.
                 */
                inline operator bool() const noexcept { return m_good; }

                /**
                 * Returns the value of the last aws error encountered by operations on this instance.
                 */
                inline int LastError() const noexcept { return m_lastError; }

                /**
                 * Creates an instance of a Streaming SHA256 HMAC.
                 */
                static HMAC CreateSHA256HMAC(Allocator *allocator, const ByteCursor &secret) noexcept;

                /**
                 * Creates an instance of a Streaming SHA256 HMAC using the Default Allocator.
                 */
                static HMAC CreateSHA256HMAC(const ByteCursor &secret) noexcept;

                /**
                 * Updates the running HMAC object with data in toHMAC. Returns true on success. Call
                 * LastError() for the reason this call failed.
                 */
                bool Update(const ByteCursor &toHMAC) noexcept;

                /**
                 * Finishes the running HMAC operation and writes the digest into output. The available capacity of
                 * output must be large enough for the digest. See: SHA256_DIGEST_SIZE and MD5_DIGEST_SIZE for size
                 * hints. 'truncateTo' is for if you want truncated output (e.g. you only want the first 16 bytes of a
                 * SHA256 digest. Returns true on success. Call LastError() for the reason this call failed.
                 */
                bool Digest(ByteBuf &output, size_t truncateTo = 0) noexcept;

                aws_hmac *GetUnderlyingHandle() const { return m_hmac; }

              protected:
                HMAC(aws_hmac *hmac) noexcept;
                HMAC() = delete;

              private:
                aws_hmac *m_hmac;
                bool m_good;
                int m_lastError;
            };

#ifdef BYO_CRYPTO
            class AWS_CRT_CPP_API ByoHMAC : public HMAC, std::enable_shared_from_this<ByoHMAC>
            {
              public:
                virtual ~ByoHMAC() = default;

              protected:
                ByoHMAC(size_t digestSize, const ByteCursor &secret, Allocator *allocator = g_allocator);

                virtual bool UpdateInternal(const ByteCursor &toHash) noexcept = 0;
                virtual bool DigestInternal(ByteBuf &output, size_t truncateTo = 0) noexcept = 0;

              private:
                static void s_Destroy(struct aws_hmac *hmac);
                static int s_Update(struct aws_hmac *hmac, const struct aws_byte_cursor *buf);
                static int s_Finalize(struct aws_hmac *hmac, struct aws_byte_buf *out);

                static aws_hmac_vtable s_Vtable;
                aws_hmac m_hmacValue;
                std::shared_ptr<ByoHMAC> m_selfReference;
            };

            using CreateHMACCallback =
                std::function<std::shared_ptr<ByoHMAC>(size_t, const ByteCursor &secret, Allocator *)>;
#endif /* BYO_CRYPTO */

        } // namespace Crypto
    }     // namespace Crt
} // namespace Aws
