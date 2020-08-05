/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/checksums/private/crc_priv.h>

#include <aws/common/macros.h>

/* Fail gracefully. Even though the we might be able to detect the presence of the instruction
 * we might not have a compiler that supports assembling those instructions.
 */
uint32_t aws_checksums_crc32c_hw(const uint8_t *input, int length, uint32_t previousCrc32) {
    return aws_checksums_crc32c_sw(input, length, previousCrc32);
}

uint32_t aws_checksums_crc32_hw(const uint8_t *input, int length, uint32_t previousCrc32) {
    return aws_checksums_crc32_sw(input, length, previousCrc32);
}
