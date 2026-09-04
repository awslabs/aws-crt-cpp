#pragma once

/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Types.h>

namespace Aws
{
    namespace Crt
    {
        /*
         * We're stuck with Aws::Crt::ByteBuf being a C-struct typedef.  So this type wraps a C aws_byte_buf object
         * and adds C++ move and copy semantics on top to allow us to safely and easily work with raw bytes destined
         * to go to aws-c-* APIs.  Primarily exists in service of configuration structures that contain binary data
         * fields that we'll pass to C.  If we embedded raw byte_bufs in these classes then we'd have to carefully
         * write out every move and copy API (or forbid them which isn't great for usability).  If we use this instead,
         * we can just "= default" and get correct behavior.
         */
        class ManagedByteBuffer
        {
          public:
            ManagedByteBuffer() noexcept;

            explicit ManagedByteBuffer(const struct aws_byte_buf &buf) noexcept;
            explicit ManagedByteBuffer(struct aws_byte_cursor cursor, Allocator *allocator = ApiAllocator()) noexcept;
            explicit ManagedByteBuffer(const char *cstring, Allocator *allocator = ApiAllocator()) noexcept;
            explicit ManagedByteBuffer(Aws::Crt::String value) noexcept;
            explicit ManagedByteBuffer(const Aws::Crt::String &value) noexcept;
            ManagedByteBuffer(uint8_t *data, size_t len, Allocator *allocator = ApiAllocator()) noexcept;

            ManagedByteBuffer(const ManagedByteBuffer &rhs) noexcept;
            ManagedByteBuffer(ManagedByteBuffer &&rhs) noexcept;
            ManagedByteBuffer &operator=(const ManagedByteBuffer &rhs) noexcept;
            ManagedByteBuffer &operator=(ManagedByteBuffer &&rhs) noexcept;

            ~ManagedByteBuffer();

            struct aws_byte_cursor cursor() const noexcept { return aws_byte_cursor_from_buf(&m_buffer); }

          private:
            struct aws_byte_buf m_buffer;
        };
    } // namespace Crt
} // namespace Aws
