/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/http/HttpProxyStrategy.h>

#include <aws/http/proxy_strategy.h>

namespace Aws
{
    namespace Crt
    {
        namespace Http
        {
            HttpProxyStrategy::~HttpProxyStrategy() { aws_http_proxy_strategy_release(m_strategy); }

            std::shared_ptr<HttpProxyStrategy> HttpProxyStrategy::CreateBasicHttpProxyStrategy(
                enum aws_http_proxy_connection_type connectionType,
                const String &username,
                const String &password,
                Allocator *allocator)
            {
                struct aws_http_proxy_strategy_basic_auth_options config;
                AWS_ZERO_STRUCT(config);
                config.proxy_connection_type = connectionType;
                config.user_name = aws_byte_cursor_from_c_str(username.c_str());
                config.password = aws_byte_cursor_from_c_str(password.c_str());

                struct aws_http_proxy_strategy *strategy = aws_http_proxy_strategy_new_basic_auth(allocator, &config);
                if (strategy == NULL)
                {
                    return NULL;
                }

                return Aws::Crt::MakeShared<HttpProxyStrategy>(allocator, strategy);
            }

            HttpProxyStrategy::HttpProxyStrategy(struct aws_http_proxy_strategy *strategy) : m_strategy(strategy) {}
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
