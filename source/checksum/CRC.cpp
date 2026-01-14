/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/checksum/CRC.h>

#include <aws/checksums/crc.h>

namespace Aws
{
    namespace Crt
    {
        namespace Checksum
        {
            uint32_t ComputeCRC32(ByteCursor input, uint32_t previousCRC32) noexcept
            {
                return aws_checksums_crc32_ex(input.ptr, input.len, previousCRC32);
            }

            uint32_t ComputeCRC32C(ByteCursor input, uint32_t previousCRC32C) noexcept
            {
                return aws_checksums_crc32c_ex(input.ptr, input.len, previousCRC32C);
            }

            uint64_t ComputeCRC64NVME(ByteCursor input, uint64_t previousCRC64NVME) noexcept
            {
                return aws_checksums_crc64nvme_ex(input.ptr, input.len, previousCRC64NVME);
            }

            uint32_t CombineCRC32(uint32_t crc1, uint32_t crc2, uint64_t len2) noexcept
            {
                return aws_checksums_crc32_combine(crc1, crc2, len2);
            }

            uint32_t CombineCRC32C(uint32_t crc1, uint32_t crc2, uint64_t len2) noexcept
            {
                return aws_checksums_crc32c_combine(crc1, crc2, len2);
            }

            uint64_t CombineCRC64NVME(uint64_t crc1, uint64_t crc2, uint64_t len2) noexcept
            {
                return aws_checksums_crc64nvme_combine(crc1, crc2, len2);
            }

        } // namespace Checksum
    } // namespace Crt
} // namespace Aws
