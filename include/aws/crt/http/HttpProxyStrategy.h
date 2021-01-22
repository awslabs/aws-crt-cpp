#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Types.h>

#include <memory>

#include <aws/http/proxy_strategy.h>

namespace Aws
{
    namespace Crt
    {
        namespace Http
        {
            class HttpProxyStrategy
            {
              public:
                HttpProxyStrategy(struct aws_http_proxy_strategy *strategy);
                virtual ~HttpProxyStrategy();

                struct aws_http_proxy_strategy *GetUnderlyingHandle() const noexcept { return m_strategy; }

                static std::shared_ptr<HttpProxyStrategy> CreateBasicHttpProxyStrategy(
                    enum aws_http_proxy_connection_type connectionType,
                    const String &username,
                    const String &password,
                    Allocator *allocator = g_allocator);

                static std::shared_ptr<HttpProxyStrategy> CreateAdaptiveKerberosHttpProxyStrategy(
                    Allocator *allocator = g_allocator);

              private:
                struct aws_http_proxy_strategy *m_strategy;
            };
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
