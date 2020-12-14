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
            class HttpProxyStrategyFactory
            {
              public:
                HttpProxyStrategyFactory(struct aws_http_proxy_strategy_factory *factory);
                virtual ~HttpProxyStrategyFactory();

                struct aws_http_proxy_strategy_factory *GetUnderlyingHandle() const noexcept { return m_factory; }

                static std::shared_ptr<HttpProxyStrategyFactory> CreateBasicHttpProxyStrategyFactory(
                    enum aws_http_proxy_connection_type connectionType,
                    const String &username,
                    const String &password,
                    Allocator *allocator = g_allocator);

                static std::shared_ptr<HttpProxyStrategyFactory> CreateExperimentalHttpProxyStrategyFactory(
                    const String &username,
                    const String &password,
                    Allocator *allocator = g_allocator);

              private:
                struct aws_http_proxy_strategy_factory *m_factory;
            };
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
