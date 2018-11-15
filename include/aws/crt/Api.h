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
#include <aws/crt/Types.h>
#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/io/TLSOptions.h>
#include <aws/crt/mqtt/MqttClient.h>

#include <aws/io/socket.h>

namespace Aws
{
    namespace Crt
    {
        class AWS_CRT_CPP_API ApiHandle
        {
        public:
            ApiHandle(Allocator* allocator) noexcept;
            ApiHandle() noexcept;
            ~ApiHandle();
            ApiHandle(const ApiHandle&) = delete;
            ApiHandle(ApiHandle&&) = delete;
            ApiHandle& operator =(const ApiHandle&) = delete;
            ApiHandle& operator =(ApiHandle&&) = delete;
        };

        AWS_CRT_CPP_API Allocator* DefaultAllocator() noexcept;
        AWS_CRT_CPP_API void LoadErrorStrings() noexcept;
        AWS_CRT_CPP_API const char* ErrorDebugString(int error) noexcept;

        AWS_CRT_CPP_API ByteBuf ByteBufFromCString(const char* str) noexcept;
        AWS_CRT_CPP_API ByteBuf ByteBufFromArray(const uint8_t *array, size_t len) noexcept;

    }
}