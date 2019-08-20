/*
 * Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <aws/crt/ByteBuf.h>

#include <aws/crt/Api.h>

namespace Aws
{
    namespace Crt
    {
        ///////////////////////////////////////////////////////////////////////////////////

        ByteCursor::ByteCursor() noexcept : m_cursor{0} {}

        ByteCursor::ByteCursor(const char *str) noexcept : m_cursor(aws_byte_cursor_from_c_str(str)) {}

        ByteCursor::ByteCursor(const String &str) noexcept
            : m_cursor(aws_byte_cursor_from_array(str.c_str(), str.size()))
        {
        }

        ByteCursor::ByteCursor(aws_byte_cursor cursor) noexcept : m_cursor(cursor) {}

        ByteCursor::ByteCursor(const aws_byte_buf &buffer) noexcept : m_cursor(aws_byte_cursor_from_buf(&buffer)) {}

        ByteCursor::ByteCursor(const uint8_t *array, size_t len) noexcept
            : m_cursor(aws_byte_cursor_from_array(array, len))
        {
        }

        ByteCursor::ByteCursor(const ByteCursor &rhs) noexcept : m_cursor(rhs.m_cursor) {}

        ByteCursor &ByteCursor::operator=(const ByteCursor &rhs) noexcept
        {
            m_cursor = rhs.m_cursor;
            return *this;
        }

        void ByteCursor::Advance(size_t len) noexcept { m_cursor = aws_byte_cursor_advance(&m_cursor, len); }

        ///////////////////////////////////////////////////////////////////////////////////

        ByteBuf::ByteBuf() noexcept : m_buffer{0}, m_bufferPtr{&m_buffer}, m_initializationErrorCode{AWS_ERROR_SUCCESS}
        {
        }

        ByteBuf::ByteBuf(const ByteBuf &rhs) noexcept : m_initializationErrorCode(rhs.m_initializationErrorCode)
        {
            if (m_initializationErrorCode != AWS_ERROR_SUCCESS)
            {
                AWS_ZERO_STRUCT(m_buffer);
                m_bufferPtr = &m_buffer;
                return;
            }

            if (rhs.m_bufferPtr == &rhs.m_buffer)
            {
                if (rhs.m_buffer.allocator != nullptr)
                {
                    if (aws_byte_buf_init_copy(&m_buffer, rhs.m_buffer.allocator, &rhs.m_buffer))
                    {
                        OnInitializationFail();
                        return;
                    }
                }
                else
                {
                    m_buffer = rhs.m_buffer;
                }
                m_bufferPtr = &m_buffer;
            }
            else
            {
                m_bufferPtr = rhs.m_bufferPtr;
            }
        }

        ByteBuf::ByteBuf(ByteBuf &&rhs) noexcept
        {
            if (rhs.m_bufferPtr == &rhs.m_buffer)
            {
                m_buffer = rhs.m_buffer;
                m_bufferPtr = &m_buffer;
                m_initializationErrorCode = rhs.m_initializationErrorCode;

                AWS_ZERO_STRUCT(rhs.m_buffer);
            }
            else
            {
                AWS_ZERO_STRUCT(m_buffer);
                m_bufferPtr = rhs.m_bufferPtr;
                m_initializationErrorCode = rhs.m_initializationErrorCode;
            }
        }

        ByteBuf::ByteBuf(Allocator *alloc, size_t capacity) noexcept : m_initializationErrorCode(AWS_ERROR_SUCCESS)
        {
            if (aws_byte_buf_init(&m_buffer, alloc, capacity))
            {
                OnInitializationFail();
                return;
            }

            m_bufferPtr = &m_buffer;
        }

        ByteBuf::ByteBuf(const uint8_t *array, size_t capacity, size_t len) noexcept
            : m_buffer(aws_byte_buf_from_array(array, capacity)), m_bufferPtr(&m_buffer),
              m_initializationErrorCode(AWS_ERROR_SUCCESS)
        {
            AWS_FATAL_ASSERT(len <= capacity);
            m_buffer.len = len;
        }

        ByteBuf::ByteBuf(aws_byte_buf *buffer) noexcept
            : m_buffer(), m_bufferPtr(buffer), m_initializationErrorCode(AWS_ERROR_SUCCESS)
        {
            AWS_ZERO_STRUCT(m_buffer);
        }

        ByteBuf &ByteBuf::operator=(const ByteBuf &rhs) noexcept
        {
            if (&rhs != this)
            {
                ByteBuf temp = rhs;
                if (!temp)
                {
                    OnInitializationFail();
                    return *this;
                }

                Cleanup();

                *this = std::move(temp);
            }

            return *this;
        }

        ByteBuf &ByteBuf::operator=(ByteBuf &&rhs) noexcept
        {
            if (&rhs != this)
            {
                Cleanup();
                if (rhs.m_bufferPtr == &rhs.m_buffer)
                {
                    m_buffer = rhs.m_buffer;
                    m_bufferPtr = &m_buffer;
                    m_initializationErrorCode = rhs.m_initializationErrorCode;

                    AWS_ZERO_STRUCT(rhs.m_buffer);
                }
                else
                {
                    m_bufferPtr = rhs.m_bufferPtr;
                    m_initializationErrorCode = rhs.m_initializationErrorCode;
                }
            }
            return *this;
        }

        ByteBuf::~ByteBuf() { Cleanup(); }

        ByteCursor ByteBuf::GetCursor() const noexcept { return ByteCursor(m_bufferPtr->buffer, m_bufferPtr->len); }

        AwsCrtResultVoid ByteBuf::Append(ByteCursor cursor) noexcept
        {
            if (aws_byte_buf_append(m_bufferPtr, cursor.GetImpl()))
            {
                return MakeLastErrorResult<void>();
            }

            return AwsCrtResultVoid();
        }

        AwsCrtResultVoid ByteBuf::AppendDynamic(ByteCursor cursor) noexcept
        {
            if (aws_byte_buf_append_dynamic(m_bufferPtr, cursor.GetImpl()))
            {
                return MakeLastErrorResult<void>();
            }

            return AwsCrtResultVoid();
        }

        void ByteBuf::Cleanup() noexcept
        {
            if (m_bufferPtr == &m_buffer)
            {
                aws_byte_buf_clean_up(&m_buffer);
                AWS_ZERO_STRUCT(m_buffer);
                m_bufferPtr = nullptr;
            }
        }

        void ByteBuf::OnInitializationFail() noexcept
        {
            AWS_ZERO_STRUCT(m_buffer);
            m_bufferPtr = &m_buffer;
            m_initializationErrorCode = LastErrorOrUnknown();
        }
    } // namespace Crt
} // namespace Aws
