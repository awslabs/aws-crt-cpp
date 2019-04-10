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

#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>
#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/io/HostResolver.h>

#include <aws/io/channel_bootstrap.h>
#include <aws/io/host_resolver.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            class AWS_CRT_CPP_API ClientBootstrap final
            {
              public:
                ClientBootstrap(
                    EventLoopGroup &elGroup,
                    HostResolver &resolver,
                    Allocator *allocator = DefaultAllocator()) noexcept;
                ~ClientBootstrap();
                ClientBootstrap(const ClientBootstrap &) = delete;
                ClientBootstrap &operator=(const ClientBootstrap &) = delete;
                ClientBootstrap(ClientBootstrap &&) = delete;
                ClientBootstrap &operator=(ClientBootstrap &&) = delete;

                operator bool() const noexcept;
                int LastError() const noexcept;

                aws_client_bootstrap *GetUnderlyingHandle() noexcept;

              private:
                aws_client_bootstrap *m_bootstrap;
                aws_host_resolution_config m_resolve_config;
                int m_lastError;
            };
        } // namespace Io
    }     // namespace Crt
} // namespace Aws