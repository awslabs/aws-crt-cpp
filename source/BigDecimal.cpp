/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/BigDecimal.h>

#include <aws/crt/Api.h>

namespace Aws
{
    namespace Crt
    {
        BigDecimal::BigDecimal() noexcept : BigDecimal(ApiAllocator()) {}

        BigDecimal::BigDecimal(Allocator *allocator) noexcept
            : BigDecimal(aws_big_decimal_new_from_cursor(allocator, aws_byte_cursor_from_c_str("0")))
        {
        }

        Optional<BigDecimal> BigDecimal::FromString(ByteCursor str, Allocator *allocator) noexcept
        {
            auto *cval = aws_big_decimal_new_from_cursor(allocator, str);
            if (cval == nullptr)
            {
                return {};
            }

            return BigDecimal(cval);
        }

        String BigDecimal::ToString() const noexcept
        {
            String str;

            auto cap = aws_big_decimal_max_strlen(m_value.get());
            str.resize(cap);

            auto buf = aws_byte_buf_from_empty_array(str.data(), cap);
            if (aws_byte_buf_append_big_decimal(&buf, m_value.get()) != AWS_OP_SUCCESS)
            {
                return {};
            }

            str.resize(buf.len);
            return str;
        }

        bool BigDecimal::operator==(const BigDecimal &other) const noexcept
        {
            return aws_big_decimal_eq(m_value.get(), other.m_value.get());
        }

        bool BigDecimal::operator!=(const BigDecimal &other) const noexcept
        {
            return !(*this == other);
        }
    } // namespace Crt
} // namespace Aws
