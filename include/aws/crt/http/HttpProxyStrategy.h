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
            struct AWS_CRT_CPP_API HttpProxyStrategyBasicAuthConfig
            {
                HttpProxyStrategyBasicAuthConfig() : ConnectionType(AWS_HPCT_HTTP_TUNNEL), Username(), Password() {}

                enum aws_http_proxy_connection_type ConnectionType;

                String Username;

                String Password;
            };

            using KerberosGetTokenFunction = std::function<bool(String &)>;
            using NtlmGetTokenFunction = std::function<bool(const String &, String &)>;

            struct AWS_CRT_CPP_API HttpProxyStrategyAdaptiveConfig
            {
                HttpProxyStrategyAdaptiveConfig() : KerberosGetToken(), NtlmGetCredential(),NtlmGetToken() {}

                KerberosGetTokenFunction KerberosGetToken;

                KerberosGetTokenFunction NtlmGetCredential;

                NtlmGetTokenFunction NtlmGetToken;
            };

            class AWS_CRT_CPP_API HttpProxyStrategy
            {
              public:
                HttpProxyStrategy(struct aws_http_proxy_strategy *strategy);
                virtual ~HttpProxyStrategy();

                struct aws_http_proxy_strategy *GetUnderlyingHandle() const noexcept { return m_strategy; }

                static std::shared_ptr<HttpProxyStrategy> CreateBasicHttpProxyStrategy(
                    const HttpProxyStrategyBasicAuthConfig &config,
                    Allocator *allocator = g_allocator);

                static std::shared_ptr<HttpProxyStrategy> CreateAdaptiveHttpProxyStrategy(
                    const HttpProxyStrategyAdaptiveConfig &config,
                    Allocator *allocator = g_allocator);

              protected:
                struct aws_http_proxy_strategy *m_strategy;
            };
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
