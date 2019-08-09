#pragma once
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
#include <aws/crt/ByteBuf.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/Types.h>

#include <aws/common/uuid.h>

namespace Aws
{
    namespace Crt
    {
        class UUID final
        {
          public:
            UUID() noexcept;
            UUID(const String &str) noexcept;

            UUID &operator=(const String &str) noexcept;

            bool operator==(const UUID &other) noexcept;
            bool operator!=(const UUID &other) noexcept;
            operator String() const;

            AwsCrtResult<ByteBuf> ToByteBuf() const noexcept;

            inline operator bool() const noexcept { return m_good; }

            int GetLastError() const noexcept;

            String ToString() const;

          private:
            aws_uuid m_uuid;
            bool m_good;
        };
    } // namespace Crt
} // namespace Aws
