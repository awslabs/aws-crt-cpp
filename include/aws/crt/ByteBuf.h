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
            ByteCursor(const uint8_t *array, size_t len) noexcept;

            ByteCursor &operator=(const ByteCursor &cursor) noexcept;

            aws_byte_cursor Get() const noexcept { return m_cursor; }
            aws_byte_cursor *GetPtr() noexcept { return &m_cursor; }

          private:
            aws_byte_cursor m_cursor;
        };

        /*
         * Base class that gets passed around by ref across CRT APIs
         */
        class ByteBuf
        {
          public:
            virtual ~ByteBuf() {}

            // All of non-init APIs go here
            AwsCrtResultVoid Append(ByteCursor cursor) noexcept;
            AwsCrtResultVoid AppendDynamic(ByteCursor cursor) noexcept;
            // etc...

            virtual aws_byte_buf *Get() noexcept = 0;
            virtual const aws_byte_buf *Get() const noexcept = 0;

          protected:
            ByteBuf() noexcept {}

            ByteBuf(const ByteBuf &rhs) noexcept = delete;
            ByteBuf(ByteBuf &&rhs) noexcept = delete;

            ByteBuf &operator=(const ByteBuf &buffer) noexcept = delete;
            ByteBuf &operator=(ByteBuf &&buffer) noexcept = delete;
        };

        /*
         * Wrapper for a pointer to a byte_buf.  Intended for byte_buf objects
         * that come from C.
         *
         * Does not do any cleanup.
         */
        class ByteBufRef : public ByteBuf
        {
          public:
            ByteBufRef() : m_buffer(nullptr) {}
            ByteBufRef(const ByteBufRef &ref) : m_buffer(ref.m_buffer) {}
            ByteBufRef(ByteBufRef &&ref) : m_buffer(ref.m_buffer) {}
            explicit ByteBufRef(aws_byte_buf *buffer) : m_buffer(buffer) {}

            virtual ~ByteBufRef() {}

            ByteBufRef &operator=(const ByteBufRef &buffer) noexcept;
            ByteBufRef &operator=(ByteBufRef &&buffer) noexcept;

            virtual aws_byte_buf *Get() noexcept override { return m_buffer; }
            virtual const aws_byte_buf *Get() const noexcept override { return m_buffer; }

          private:
            aws_byte_buf *m_buffer;
        };

        /*
         * Wrapper for a byte_buf value.  Intended for C++ user creation of
         * byte bufs that get passed into C-land via a C++ API.
         *
         * Object is cleaned up internally on destruction.
         *
         * All constructors cannot fail.  Construction patterns that may fail
         * are done via Init* methods.
         *
         * Copy-construction and copy-assignment are disallowed.  Use the (failable)
         * Init method that takes a ByteBufValue instead.
         */
        class ByteBufValue : public ByteBuf
        {
          public:
            ByteBufValue() noexcept;
            ByteBufValue(const ByteBufValue &buffer) noexcept = delete;
            ByteBufValue(ByteBufValue &&buffer) noexcept;
            explicit ByteBufValue(const char *str) noexcept;
            ByteBufValue(const uint8_t *array, size_t capacity, size_t len);

            virtual ~ByteBufValue();

            ByteBufValue &operator=(ByteBufValue &&buffer) noexcept;
            ByteBufValue &operator=(const ByteBufValue &buffer) noexcept = delete;

            static AwsCrtResult<ByteBufValue> Init(const ByteBufValue &buffer) noexcept;
            static AwsCrtResult<ByteBufValue> Init(Allocator *alloc, size_t capacity) noexcept;
            static AwsCrtResult<ByteBufValue> InitFromArray(
                Allocator *alloc,
                const uint8_t *array,
                size_t len) noexcept;

            virtual aws_byte_buf *Get() noexcept override { return &m_buffer; }
            virtual const aws_byte_buf *Get() const noexcept override { return &m_buffer; }

          private:
            void Cleanup() noexcept;

            struct aws_byte_buf m_buffer;
        };
    } // namespace Crt
} // namespace Aws
