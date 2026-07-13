#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>

#include <memory>

struct aws_s3_buffer_ticket;

namespace Aws
{
    namespace Crt
    {
        namespace S3
        {
            /**
             * Owning handle to a buffer that the CRT has loaned out as part of a
             * stream download. The CRT delivers each received part by invoking the
             * BodyCallbackEx with a borrowed ticket pointing at memory in the CRT's
             * pool. The ticket is alive only for the duration of that callback; call
             * Acquire() inside the callback to obtain a shared_ptr that keeps the
             * buffer valid until the last copy of it is destroyed.
             */
            class AWS_CRT_CPP_API S3BufferTicket final
            {
              public:
                S3BufferTicket(const S3BufferTicket &) = delete;
                S3BufferTicket(S3BufferTicket &&) = delete;
                S3BufferTicket &operator=(const S3BufferTicket &) = delete;
                S3BufferTicket &operator=(S3BufferTicket &&) = delete;

                ~S3BufferTicket() noexcept = default;

                /**
                 * Take a new reference to the underlying buffer and hand it back as
                 * a shared_ptr. Copy the returned shared_ptr freely to share the
                 * buffer across owners; the CRT reference is released once when the
                 * last copy is destroyed, so acquire/release stays hidden behind the
                 * shared_ptr interface.
                 *
                 * @return a shared handle keeping the buffer valid until the last
                 *         copy is destroyed.
                 */
                std::shared_ptr<S3BufferTicket> Acquire() noexcept;

                /**
                 * Return a cursor over the object bytes this ticket references. The
                 * cursor points directly into the CRT-owned buffer - reading from it
                 * copies nothing. It stays valid as long as this ticket (or a
                 * shared_ptr obtained from Acquire()) is alive. Returns an empty
                 * cursor if the ticket holds no buffer.
                 *
                 * @return a cursor into the buffer; empty if there is none.
                 */
                ByteCursor Claim() noexcept;

                /// @private
                /// Wraps a borrowed C ticket handle (or nullptr) without taking a
                /// reference; the wrapper never releases it. Acquire() is the only
                /// path that takes and releases a reference.
                explicit S3BufferTicket(struct aws_s3_buffer_ticket *ticket) noexcept;

              private:
                // Borrowed, non-owning: the reference is owned by the CRT (for the
                // stack ticket in the callback) or by an Acquire() shared_ptr's
                // deleter - never released by this raw handle directly.
                struct aws_s3_buffer_ticket *m_ticket;
            };

        } // namespace S3
    } // namespace Crt
} // namespace Aws
