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

#include <aws/crt/Api.h>
#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>
#include <aws/crt/io/EventLoopGroup.h>

#include <aws/io/channel_bootstrap.h>

class EventLoopGroup;

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            class AWS_CRT_CPP_API ClientBootstrap final
            {
            public:
                ClientBootstrap(const EventLoopGroup& elGroup = EventLoopGroup(),
                        Allocator* allocator = DefaultAllocator()) noexcept;
                ~ClientBootstrap();
                ClientBootstrap(const ClientBootstrap&) = delete;
                ClientBootstrap& operator=(const ClientBootstrap&) = delete;
                ClientBootstrap(ClientBootstrap&&) noexcept;
                ClientBootstrap& operator=(ClientBootstrap&&) noexcept;

                operator bool() noexcept;
                int LastError() noexcept;

                const aws_client_bootstrap* GetUnderlyingHandle() const;
            private:
                aws_client_bootstrap m_bootstrap;
                int m_lastError;
            };
        }
    }
}