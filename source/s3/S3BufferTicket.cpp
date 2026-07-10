/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/s3/S3BufferTicket.h>

#include <aws/crt/Api.h>

#include <aws/s3/s3_buffer_pool.h>

namespace Aws
{
    namespace Crt
    {
        namespace S3
        {
            // Non-owning: wraps a borrowed CRT ticket handle. The wrapper never
            // releases the handle itself - the reference (if any) is owned by
            // the caller. The stack ticket handed to BodyCallbackEx uses this
            // directly; Acquire() takes a reference and puts the matching
            // release in its shared_ptr deleter.
            S3BufferTicket::S3BufferTicket(struct aws_s3_buffer_ticket *ticket) noexcept : m_ticket(ticket) {}

            std::shared_ptr<S3BufferTicket> S3BufferTicket::Acquire() noexcept
            {
                if (m_ticket == nullptr)
                {
                    return nullptr;
                }
                // Take a reference for the caller, then hand back a heap wrapper
                // whose deleter both releases that reference and frees the
                // wrapper through the CRT allocator. This is the only path that
                // allocates: the per-chunk callback ticket stays on the stack.
                aws_s3_buffer_ticket_acquire(m_ticket);
                struct aws_s3_buffer_ticket *acquired = m_ticket;
                return std::shared_ptr<S3BufferTicket>(
                    Aws::Crt::New<S3BufferTicket>(ApiAllocator(), acquired),
                    [acquired](S3BufferTicket *p)
                    {
                        aws_s3_buffer_ticket_release(acquired);
                        Delete(p, ApiAllocator());
                    });
            }

            ByteCursor S3BufferTicket::Claim() noexcept
            {
                if (m_ticket == nullptr)
                {
                    return ByteCursor{0, nullptr};
                }
                struct aws_byte_buf buffer = aws_s3_buffer_ticket_claim(m_ticket);
                return ByteCursorFromByteBuf(buffer);
            }

        } // namespace S3
    } // namespace Crt
} // namespace Aws
