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
        uint32_t crc32(const uint8_t *input, size_t length, uint32_t prev);
        uint32_t crc32c(const uint8_t *input, size_t length, uint32_t prev);
    } // namespace Crt
} // namespace Aws
