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
#include <aws/crt/Types.h>

namespace Aws
{
    namespace Crt
    {
        Allocator* DefaultAllocator() noexcept
        {
            return aws_default_allocator();
        }

        ByteBuf ByteBufFromCString(const char* str) noexcept
        {
            return aws_byte_buf_from_c_str(str);
        }

        ByteBuf ByteBufFromArray(const uint8_t *array, size_t len) noexcept
        {
            return aws_byte_buf_from_array(array, len);
        }
    }
}
