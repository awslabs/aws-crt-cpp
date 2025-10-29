/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/crypto/HKDF.h>

#include <aws/cal/hkdf.h>

namespace Aws
{
    namespace Crt
    {
        namespace Crypto
        {
            bool DeriveSHA512HMACHKDF(
                Allocator *allocator,
                ByteCursor ikm,
                ByteCursor salt,
                ByteCursor info,
                ByteBuf &out,
                size_t length) noexcept
            {
                return aws_hkdf_derive(allocator, HKDF_HMAC_SHA512, ikm, salt, info, &out, length) == AWS_OP_SUCCESS;
            }
        } // namespace Crypto
    } // namespace Crt
} // namespace Aws
