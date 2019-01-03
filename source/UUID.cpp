/*
 * Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
#include <aws/crt/UUID.h>

namespace Aws
{
    namespace Crt
    {
        UUID::UUID() noexcept : m_good(false)
        {
            if (aws_uuid_init(&m_uuid) == AWS_OP_SUCCESS)
            {
                m_good = true;
            }
        }

        UUID::UUID(const String &str) noexcept : m_good(false)
        {
            auto strCur = aws_byte_cursor_from_c_str(str.c_str());
            if (aws_uuid_init_from_str(&m_uuid, &strCur) == AWS_OP_SUCCESS)
            {
                m_good = true;
            }
        }

        UUID &UUID::operator=(const String &str) noexcept
        {
            *this = UUID(str);
            return *this;
        }

        bool UUID::operator==(const UUID &other) noexcept { return aws_uuid_equals(&m_uuid, &other.m_uuid); }

        bool UUID::operator!=(const UUID &other) noexcept { return !aws_uuid_equals(&m_uuid, &other.m_uuid); }

        String UUID::ToString() const
        {
            String uuidStr;
            uuidStr.reserve(AWS_UUID_STR_LEN);

            auto outBuf = ByteBufFromArray(reinterpret_cast<const uint8_t *>(uuidStr.data()), uuidStr.capacity());
            outBuf.len = 0;
            aws_uuid_to_str(&m_uuid, &outBuf);
            return uuidStr;
        }

        UUID::operator String() const { return ToString(); }

        UUID::operator ByteBuf() const noexcept { return ByteBufFromArray(m_uuid.uuid_data, sizeof(m_uuid.uuid_data)); }

        int UUID::GetLastError() const noexcept { return aws_last_error(); }
    } // namespace Crt
} // namespace Aws