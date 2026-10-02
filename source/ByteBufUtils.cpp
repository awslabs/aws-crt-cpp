/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/ByteBufUtils.h>

namespace Aws
{
    namespace Crt
    {
        /*
         * Basic invariant
         *   m_buffer is always
         *     (1) zeroed, or
         *     (2) is valid and references a sequence of one or more bytes
         */

        ManagedByteBuffer::ManagedByteBuffer() noexcept
        {
            AWS_ZERO_STRUCT(m_buffer);
        }

        ManagedByteBuffer::ManagedByteBuffer(const struct aws_byte_buf &buf) noexcept
        {
            struct aws_allocator *allocator = buf.allocator;
            size_t len = buf.len;
            if (allocator && len > 0)
            {
                aws_byte_buf_init_copy_from_cursor(&m_buffer, allocator, aws_byte_cursor_from_buf(&buf));
            }
            else
            {
                AWS_ZERO_STRUCT(m_buffer);
            }
        }

        ManagedByteBuffer::ManagedByteBuffer(struct aws_byte_cursor cursor, Allocator *allocator) noexcept
        {
            size_t len = cursor.len;
            if (allocator && len > 0)
            {
                aws_byte_buf_init_copy_from_cursor(&m_buffer, allocator, cursor);
            }
            else
            {
                AWS_ZERO_STRUCT(m_buffer);
            }
        }

        ManagedByteBuffer::ManagedByteBuffer(const char *cstring, Allocator *allocator) noexcept
        {
            size_t len = strlen(cstring);
            if (allocator && len > 0)
            {
                aws_byte_buf_init_copy_from_cursor(&m_buffer, allocator, aws_byte_cursor_from_array(cstring, len));
            }
            else
            {
                AWS_ZERO_STRUCT(m_buffer);
            }
        }

        ManagedByteBuffer::ManagedByteBuffer(Aws::Crt::String value) noexcept
        {
            struct aws_allocator *allocator = value.get_allocator().m_allocator;
            size_t len = value.size();
            if (allocator && len > 0)
            {
                aws_byte_buf_init_copy_from_cursor(&m_buffer, allocator, aws_byte_cursor_from_array(value.data(), len));
            }
            else
            {
                AWS_ZERO_STRUCT(m_buffer);
            }
        }

        ManagedByteBuffer::ManagedByteBuffer(const Aws::Crt::String &value) noexcept
        {
            struct aws_allocator *allocator = value.get_allocator().m_allocator;
            size_t len = value.size();
            if (allocator && len > 0)
            {
                aws_byte_buf_init_copy_from_cursor(&m_buffer, allocator, aws_byte_cursor_from_array(value.data(), len));
            }
            else
            {
                AWS_ZERO_STRUCT(m_buffer);
            }
        }

        ManagedByteBuffer::ManagedByteBuffer(uint8_t *data, size_t len, Allocator *allocator) noexcept
        {
            if (allocator && len > 0)
            {
                aws_byte_buf_init_copy_from_cursor(&m_buffer, allocator, aws_byte_cursor_from_array(data, len));
            }
            else
            {
                AWS_ZERO_STRUCT(m_buffer);
            }
        }

        ManagedByteBuffer::ManagedByteBuffer(const ManagedByteBuffer &rhs) noexcept
        {
            struct aws_allocator *allocator = rhs.m_buffer.allocator;
            if (allocator && rhs.m_buffer.len > 0)
            {
                aws_byte_buf_init_copy_from_cursor(&m_buffer, allocator, aws_byte_cursor_from_buf(&rhs.m_buffer));
            }
            else
            {
                AWS_ZERO_STRUCT(m_buffer);
            }
        }

        ManagedByteBuffer::ManagedByteBuffer(ManagedByteBuffer &&rhs) noexcept
        {
            m_buffer = rhs.m_buffer;
            AWS_ZERO_STRUCT(rhs.m_buffer);
        }

        ManagedByteBuffer &ManagedByteBuffer::operator=(const ManagedByteBuffer &rhs) noexcept
        {
            if (this != &rhs)
            {
                aws_byte_buf_clean_up(&m_buffer);

                struct aws_allocator *allocator = rhs.m_buffer.allocator;
                if (allocator && rhs.m_buffer.len > 0)
                {
                    aws_byte_buf_init_copy_from_cursor(&m_buffer, allocator, aws_byte_cursor_from_buf(&rhs.m_buffer));
                }
                else
                {
                    AWS_ZERO_STRUCT(m_buffer);
                }
            }

            return *this;
        }

        ManagedByteBuffer &ManagedByteBuffer::operator=(ManagedByteBuffer &&rhs) noexcept
        {
            if (this != &rhs)
            {
                aws_byte_buf_clean_up(&m_buffer);
                m_buffer = rhs.m_buffer;
                AWS_ZERO_STRUCT(rhs.m_buffer);
            }

            return *this;
        }

        ManagedByteBuffer::~ManagedByteBuffer()
        {
            aws_byte_buf_clean_up(&m_buffer);
        }
    } // namespace Crt
} // namespace Aws