/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/checksums/crc.h>
#include <aws/common/byte_buf.h>
#include <aws/crt/checksums/Checksums.h>

namespace Aws
{
    namespace Crt
    {
        uint32_t crcCommon(
            const uint8_t *input,
            size_t length,
            uint32_t prev,
            uint32_t (*checksum_fn)(const uint8_t *, int, uint32_t))
        {
            struct aws_byte_cursor cursor;
            cursor.len = length;
            cursor.ptr = (uint8_t *)input;
            uint32_t res = prev;
            while (cursor.len > INT_MAX)
            {
                res = checksum_fn(cursor.ptr, INT_MAX, res);
                aws_byte_cursor_advance(&cursor, INT_MAX);
            }
            return checksum_fn(cursor.ptr, (int)cursor.len, res);
        }
        uint32_t crc32(const uint8_t *input, size_t length, uint32_t prev)
        {
            return crcCommon(input, length, prev, aws_checksums_crc32);
        }
        uint32_t crc32c(const uint8_t *input, size_t length, uint32_t prev)
        {
            return crcCommon(input, length, prev, aws_checksums_crc32c);
        }
    } // namespace Crt
} // namespace Aws
