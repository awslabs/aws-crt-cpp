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

        ByteCursor::ByteCursor() noexcept { AWS_ZERO_STRUCT(m_cursor); }

        ByteCursor::ByteCursor(const char *str) noexcept { m_cursor = aws_byte_cursor_from_c_str(str); }

        ByteCursor::ByteCursor(const String &str) noexcept
        {
            m_cursor = aws_byte_cursor_from_array(str.c_str(), str.size());
        }

        ByteCursor::ByteCursor(aws_byte_cursor cursor) noexcept : m_cursor(cursor) {}

        ByteCursor::ByteCursor(const uint8_t *array, size_t len) noexcept
        {
            m_cursor = aws_byte_cursor_from_array(array, len);
        }

        ByteCursor::ByteCursor(const ByteCursor &cursor) noexcept { m_cursor = cursor.m_cursor; }

        ByteCursor &ByteCursor::operator=(const ByteCursor &cursor) noexcept
        {
            m_cursor = cursor.m_cursor;

            return *this;
        }

        ///////////////////////////////////////////////////////////////////////////////////

        AwsCrtResultVoid ByteBuf::Append(ByteCursor cursor) noexcept
        {
            struct aws_byte_buf *buffer = Get();
            if (aws_byte_buf_append(buffer, cursor.GetPtr()))
            {
                return MakeLastErrorResult<void>();
            }

            return AwsCrtResultVoid();
        }

        AwsCrtResultVoid ByteBuf::AppendDynamic(ByteCursor cursor) noexcept
        {
            struct aws_byte_buf *buffer = Get();
            if (aws_byte_buf_append_dynamic(buffer, cursor.GetPtr()))
            {
                return MakeLastErrorResult<void>();
            }

            return AwsCrtResultVoid();
        }

        ///////////////////////////////////////////////////////////////////////////////////

        ByteBufRef &ByteBufRef::operator=(const ByteBufRef &buffer) noexcept
        {
            m_buffer = buffer.m_buffer;
            return *this;
        }

        ByteBufRef &ByteBufRef::operator=(ByteBufRef &&buffer) noexcept
        {
            m_buffer = buffer.m_buffer;
            return *this;
        }

        ///////////////////////////////////////////////////////////////////////////////////

        ByteBufValue::ByteBufValue() noexcept { AWS_ZERO_STRUCT(m_buffer); }

        ByteBufValue::ByteBufValue(ByteBufValue &&buffer) noexcept : m_buffer(buffer.m_buffer)
        {
            AWS_ZERO_STRUCT(buffer.m_buffer);
        }

        ByteBufValue::ByteBufValue(const char *str) noexcept : m_buffer(aws_byte_buf_from_c_str(str)) {}

        ByteBufValue::ByteBufValue(const uint8_t *array, size_t capacity, size_t len) noexcept
            : m_buffer(aws_byte_buf_from_array(array, capacity))
        {
            AWS_FATAL_ASSERT(len <= capacity);
            m_buffer.len = len;
        }

        ByteBufValue::~ByteBufValue() { Cleanup(); }

        ByteBufValue &ByteBufValue::operator=(ByteBufValue &&buffer) noexcept
        {
            if (&buffer != this)
            {
                Cleanup();
                m_buffer = buffer.m_buffer;
                AWS_ZERO_STRUCT(buffer.m_buffer);
            }

            return *this;
        }

        AwsCrtResult<ByteBufValue> ByteBufValue::Init(const ByteBufValue &buffer) noexcept
        {
            ByteBufValue temp;

            if (buffer.m_buffer.allocator != nullptr)
            {
                if (aws_byte_buf_init_copy(&temp.m_buffer, buffer.m_buffer.allocator, &buffer.m_buffer))
                {
                    return MakeLastErrorResult<ByteBufValue>();
                }
            }
            else
            {
                temp.m_buffer = buffer.m_buffer;
            }

            return AwsCrtResult<ByteBufValue>(std::move(temp));
        }

        AwsCrtResult<ByteBufValue> ByteBufValue::Init(Allocator *alloc, size_t capacity) noexcept
        {
            ByteBufValue temp;

            if (aws_byte_buf_init(&temp.m_buffer, alloc, capacity))
            {
                return MakeLastErrorResult<ByteBufValue>();
            }

            return AwsCrtResult<ByteBufValue>(std::move(temp));
        }

        AwsCrtResult<ByteBufValue> ByteBufValue::InitFromArray(
            Allocator *alloc,
            const uint8_t *array,
            size_t len) noexcept
        {
            ByteBufValue temp;

            struct aws_byte_cursor cursor = {len, const_cast<uint8_t *>(array)};
            if (aws_byte_buf_init_copy_from_cursor(&temp.m_buffer, alloc, cursor))
            {
                return MakeLastErrorResult<ByteBufValue>();
            }

            return AwsCrtResult<ByteBufValue>(std::move(temp));
        }

        void ByteBufValue::Cleanup() noexcept { aws_byte_buf_clean_up(&m_buffer); }
    } // namespace Crt
} // namespace Aws
