#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>

namespace Aws
{
    namespace Crt
    {
        namespace Crypto
        {
            /**
             * Derives an SHA256 HMAC HKDF using the default allocator and writes it to out.
             * If this function fails, Aws::Crt::LastError() will contain the error that occurred.
             */
            bool AWS_CRT_CPP_API DeriveSHA512HMACHKDF(
                Allocator *allocator,
                ByteCursor ikm,
                ByteCursor salt,
                ByteCursor info,
                ByteBuf &out,
                size_t length) noexcept;
        } // namespace Crypto
    } // namespace Crt
} // namespace Aws
