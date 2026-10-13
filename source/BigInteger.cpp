/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/BigInteger.h>

#include <aws/crt/Api.h>

namespace Aws
{
    namespace Crt
    {
        BigInteger::BigInteger() noexcept : BigInteger(ApiAllocator()) {}

        BigInteger::BigInteger(Allocator *allocator) noexcept
            : BigInteger(aws_big_integer_new(allocator, ByteCursor{0, nullptr}, false))
        {
        }

        Optional<BigInteger> BigInteger::FromString(ByteCursor str, Allocator *allocator) noexcept
        {
            auto *cval = aws_big_integer_new_from_cursor(allocator, str);
            if (cval == nullptr)
            {
                return {};
            }

            return BigInteger(cval);
        }

        String BigInteger::ToString() const noexcept
        {
            String str;

            auto cap = aws_big_integer_max_strlen(m_value.get());
            str.resize(cap);

            auto buf = aws_byte_buf_from_empty_array(str.data(), cap);
            if (aws_big_integer_to_str(m_value.get(), &buf) != AWS_OP_SUCCESS)
            {
                return {};
            }

            str.resize(buf.len);
            return str;
        }

        bool BigInteger::operator==(const BigInteger &other) const noexcept
        {
            return aws_big_integer_eq(m_value.get(), other.m_value.get());
        }

        bool BigInteger::operator!=(const BigInteger &other) const noexcept
        {
            return !(*this == other);
        }
    } // namespace Crt
} // namespace Aws
