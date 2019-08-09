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

namespace Aws
{
    namespace Crt
    {
        ///////////////////////////////////////////////////////////////////////////////////

        ByteCursor::ByteCursor() noexcept : m_cursorPtr(&m_cursor) { AWS_ZERO_STRUCT(m_cursor); }

        ByteCursor::ByteCursor(const char *str) noexcept : m_cursorPtr(&m_cursor)
        {
            m_cursor = aws_byte_cursor_from_c_str(str);
        }

        ByteCursor::ByteCursor(const String &str) noexcept : m_cursorPtr(&m_cursor)
        {
            m_cursor = aws_byte_cursor_from_array(str.c_str(), str.size());
        }

        ByteCursor::ByteCursor(aws_byte_cursor cursor) noexcept : m_cursor(cursor), m_cursorPtr(&m_cursor) {}

        ByteCursor::ByteCursor(aws_byte_cursor *cursor) noexcept : m_cursorPtr(cursor) { AWS_ZERO_STRUCT(m_cursor); }

        ByteCursor::ByteCursor(const aws_byte_buf *buffer) noexcept : m_cursorPtr(&m_cursor)
        {
            m_cursor = aws_byte_cursor_from_buf(buffer);
        }

        ByteCursor::ByteCursor(const uint8_t *array, size_t len) noexcept : m_cursorPtr(&m_cursor)
        {
            m_cursor = aws_byte_cursor_from_array(array, len);
        }

        ByteCursor::ByteCursor(const ByteCursor &cursor) noexcept : m_cursor(cursor.m_cursor), m_cursorPtr(&m_cursor) {}

        ByteCursor &ByteCursor::operator=(const ByteCursor &cursor) noexcept
        {
            m_cursor = cursor.m_cursor;
            m_cursorPtr = &m_cursor;

            return *this;
        }

        ///////////////////////////////////////////////////////////////////////////////////

        AwsCrtResultVoid ByteBuf::Append(ByteCursor cursor) noexcept
        {
            if (aws_byte_buf_append(m_bufferPtr, cursor.Get()))
            {
                return MakeLastErrorResult<void>();
            }

            return AwsCrtResultVoid();
        }

        AwsCrtResultVoid ByteBuf::AppendDynamic(ByteCursor cursor) noexcept
        {
            if (aws_byte_buf_append_dynamic(m_bufferPtr, cursor.Get()))
            {
                return MakeLastErrorResult<void>();
            }

            return AwsCrtResultVoid();
        }

        ///////////////////////////////////////////////////////////////////////////////////

        void ByteBuf::Cleanup() noexcept
        {
            if (m_bufferPtr == &m_buffer)
            {
                aws_byte_buf_clean_up(&m_buffer);
                AWS_ZERO_STRUCT(m_buffer);
                m_bufferPtr = nullptr;
            }
        }

        ByteBuf::ByteBuf(ByteBuf &&rhs) noexcept
        {
            if (rhs.m_bufferPtr == &rhs.m_buffer)
            {
                m_buffer = rhs.m_buffer;
                m_bufferPtr = &m_buffer;

                AWS_ZERO_STRUCT(rhs.m_buffer);
                rhs.m_bufferPtr = nullptr;
            }
            else
            {
                AWS_ZERO_STRUCT(m_buffer);
                m_bufferPtr = rhs.m_bufferPtr;
            }
        }

        ByteBuf::ByteBuf(aws_byte_buf *buffer) noexcept : m_bufferPtr(buffer) { AWS_ZERO_STRUCT(m_buffer); }

        ByteBuf::ByteBuf(const char *str) noexcept : m_buffer(aws_byte_buf_from_c_str(str)), m_bufferPtr(&m_buffer) {}

        ByteBuf::ByteBuf(const uint8_t *array, size_t capacity, size_t len) noexcept
            : m_buffer(aws_byte_buf_from_array(array, capacity)), m_bufferPtr(&m_buffer)
        {
            AWS_FATAL_ASSERT(len <= capacity);
            m_buffer.len = len;
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

                    AWS_ZERO_STRUCT(rhs.m_buffer);
                    rhs.m_bufferPtr = nullptr;
                }
                else
                {
                    m_bufferPtr = rhs.m_bufferPtr;
                }
            }
            return *this;
        }

        ByteBuf::ByteBuf() noexcept : m_bufferPtr(nullptr) { AWS_ZERO_STRUCT(m_buffer); }

        ByteBuf::~ByteBuf() { Cleanup(); }

        AwsCrtResult<ByteBuf> ByteBuf::Init(const ByteBuf &buffer) noexcept
        {
            ByteBuf temp;

            if (buffer.m_bufferPtr == &buffer.m_buffer)
            {
                if (buffer.m_buffer.allocator != nullptr)
                {
                    if (aws_byte_buf_init_copy(&temp.m_buffer, buffer.m_buffer.allocator, &buffer.m_buffer))
                    {
                        return MakeLastErrorResult<ByteBuf>();
                    }
                }
                else
                {
                    temp.m_buffer = buffer.m_buffer;
                }
                temp.m_bufferPtr = &temp.m_buffer;
            }
            else
            {
                temp.m_bufferPtr = buffer.m_bufferPtr;
            }

            return AwsCrtResult<ByteBuf>(std::move(temp));
        }

        AwsCrtResult<ByteBuf> ByteBuf::Init(Allocator *alloc, size_t capacity) noexcept
        {
            ByteBuf temp;

            if (aws_byte_buf_init(&temp.m_buffer, alloc, capacity))
            {
                return MakeLastErrorResult<ByteBuf>();
            }

            temp.m_bufferPtr = &temp.m_buffer;

            return AwsCrtResult<ByteBuf>(std::move(temp));
        }

        AwsCrtResult<ByteBuf> ByteBuf::InitFromArray(Allocator *alloc, const uint8_t *array, size_t len) noexcept
        {

            ByteBuf temp;
            ByteCursor cursor(const_cast<uint8_t *>(array), len);
            if (aws_byte_buf_init_copy_from_cursor(&temp.m_buffer, alloc, *cursor.Get()))
            {
                return MakeLastErrorResult<ByteBuf>();
            }

            temp.m_bufferPtr = &temp.m_buffer;

            return AwsCrtResult<ByteBuf>(std::move(temp));
        }

        ByteCursor ByteBuf::GetCursor() const noexcept { return ByteCursor(m_bufferPtr->buffer, m_bufferPtr->len); }
    } // namespace Crt
} // namespace Aws
