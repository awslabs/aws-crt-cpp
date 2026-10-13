#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>

#include <aws/common/big.h>

#include <memory>

namespace Aws
{
    namespace Crt
    {
        /**
         * Arbitrary-precision decimal, with value = mantissa * 10^exponent. Values are immutable.
         *
         * Math operations are not supported by BigDecimal, use ToString() to export to a library that does.
         */
        class AWS_CRT_CPP_API BigDecimal final
        {
          public:
            BigDecimal() noexcept;
            explicit BigDecimal(Allocator *allocator) noexcept;

            /// @private
            explicit BigDecimal(aws_big_decimal *value) noexcept : m_value(value, aws_big_decimal_destroy) {}
            /// @private
            const aws_big_decimal *GetUnderlyingHandle() const noexcept { return m_value.get(); }

            static Optional<BigDecimal> FromString(ByteCursor str, Allocator *allocator = ApiAllocator()) noexcept;

            String ToString() const noexcept;

            bool operator==(const BigDecimal &other) const noexcept;
            bool operator!=(const BigDecimal &other) const noexcept;

          private:
            std::shared_ptr<aws_big_decimal> m_value;
        };
    } // namespace Crt
} // namespace Aws
