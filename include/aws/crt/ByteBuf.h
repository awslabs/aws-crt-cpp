#pragma once
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

#include <aws/crt/Outcome.h>
#include <aws/crt/Types.h>

#include <aws/common/byte_buf.h>

namespace Aws
{
    namespace Crt
    {
        class ByteCursor
        {
          public:
            ByteCursor() noexcept;
            ByteCursor(const ByteCursor &cursor) noexcept;
            explicit ByteCursor(const char *str) noexcept;
            explicit ByteCursor(const String &str) noexcept;
            explicit ByteCursor(aws_byte_cursor cursor) noexcept;
            explicit ByteCursor(aws_byte_cursor *cursor) noexcept;
            explicit ByteCursor(const aws_byte_buf &buffer) noexcept;
            explicit ByteCursor(const aws_byte_buf *buffer) noexcept;
            ByteCursor(const uint8_t *array, size_t len) noexcept;

            ByteCursor &operator=(const ByteCursor &cursor) noexcept;

            aws_byte_cursor *Get() noexcept { return m_cursorPtr; }
            const aws_byte_cursor *Get() const noexcept { return m_cursorPtr; }

          private:
            aws_byte_cursor m_cursor;
            aws_byte_cursor *m_cursorPtr;
        };

        class ByteBuf
        {
          public:
            // Initialization that cannot fail
            ByteBuf() noexcept;
            ByteBuf(ByteBuf &&rhs) noexcept;
            explicit ByteBuf(aws_byte_buf *buffer) noexcept;
            explicit ByteBuf(const char *str) noexcept;
            ByteBuf(const uint8_t *array, size_t capacity, size_t len) noexcept;

            ByteBuf &operator=(ByteBuf &&buffer) noexcept;

            ~ByteBuf();

            // Initialization that can fail
            static AwsCrtResult<ByteBuf> Init(const ByteBuf &buffer) noexcept;
            static AwsCrtResult<ByteBuf> Init(Allocator *alloc, size_t capacity) noexcept;
            static AwsCrtResult<ByteBuf> InitFromArray(Allocator *alloc, const uint8_t *array, size_t len) noexcept;

            // All of non-init APIs go here
            AwsCrtResult<void> Append(ByteCursor cursor) noexcept;
            AwsCrtResult<void> AppendDynamic(ByteCursor cursor) noexcept;
            // etc...

            aws_byte_buf *Get() noexcept { return m_bufferPtr; }
            const aws_byte_buf *Get() const noexcept { return m_bufferPtr; }

            ByteCursor GetCursor() const noexcept;

          private:
            ByteBuf(const ByteBuf &rhs) = delete;
            ByteBuf &operator=(const ByteBuf &buffer) = delete;

            void Cleanup() noexcept;

            aws_byte_buf m_buffer;
            aws_byte_buf *m_bufferPtr;
        };
    } // namespace Crt
} // namespace Aws
