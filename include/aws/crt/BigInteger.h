#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>

#include <aws/common/bignum.h>

#include <memory>

namespace Aws
{
    namespace Crt
    {
        /**
         * Arbitrary-precision integer. Values are immutable.
         *
         * Math operations are not supported by BigInteger, use ToString() to export to a library that does.
         */
        class AWS_CRT_CPP_API BigInteger final
        {
          public:
            BigInteger() noexcept;
            explicit BigInteger(Allocator *allocator) noexcept;

            /// @private
            explicit BigInteger(aws_big_integer *value) noexcept : m_value(value, aws_big_integer_destroy) {}
            /// @private
            const aws_big_integer *GetUnderlyingHandle() const noexcept { return m_value.get(); }

            static Optional<BigInteger> FromString(ByteCursor str, Allocator *allocator = ApiAllocator()) noexcept;

            String ToString() const noexcept;

            bool operator==(const BigInteger &other) const noexcept;
            bool operator!=(const BigInteger &other) const noexcept;

          private:
            std::shared_ptr<aws_big_integer> m_value;
        };
    } // namespace Crt
} // namespace Aws
