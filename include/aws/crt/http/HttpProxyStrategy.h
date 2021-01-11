#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Types.h>

#include <memory>

#include <aws/http/proxy_strategy.h>

void send_kerberos_status(int httpStatusCode);
char *get_kerb_usertoken();
char *get_ntlm_resp();
char *get_ntlm_cred();
void send_ntlm_chall_header(size_t length, uint8_t *httpHeader, size_t length1, uint8_t *httpHeader1, size_t num_headers);

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

                static std::shared_ptr<HttpProxyStrategyFactory> CreateAdaptiveKerberosHttpProxyStrategyFactory(
                    Allocator *allocator = g_allocator);

                static std::shared_ptr<HttpProxyStrategyFactory> CreateKerberosHttpProxyStrategyFactory(
                    enum aws_http_proxy_connection_type connectionType,
                    const String &usertoken,
                    Allocator *allocator = g_allocator);

                static std::shared_ptr<HttpProxyStrategyFactory> CreateAdaptiveNtlmHttpProxyStrategyFactory(
                    Allocator *allocator = g_allocator);

              private:
                struct aws_http_proxy_strategy_factory *m_factory;
            };
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
