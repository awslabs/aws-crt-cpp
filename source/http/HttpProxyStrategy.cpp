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
            HttpProxyStrategyFactory::~HttpProxyStrategyFactory()
            {
                aws_http_proxy_strategy_factory_release(m_factory);
            }

            std::shared_ptr<HttpProxyStrategyFactory> HttpProxyStrategyFactory::CreateBasicHttpProxyStrategyFactory(
                enum aws_http_proxy_connection_type connectionType,
                const String &username,
                const String &password,
                Allocator *allocator)
            {
                struct aws_http_proxy_strategy_factory_basic_auth_config config;
                AWS_ZERO_STRUCT(config);
                config.proxy_connection_type = connectionType;
                config.user_name = aws_byte_cursor_from_c_str(username.c_str());
                config.password = aws_byte_cursor_from_c_str(password.c_str());

                struct aws_http_proxy_strategy_factory *factory =
                    aws_http_proxy_strategy_factory_new_basic_auth(allocator, &config);
                if (factory == NULL)
                {
                    return NULL;
                }

                return Aws::Crt::MakeShared<HttpProxyStrategyFactory>(allocator, factory);
            }

            std::shared_ptr<HttpProxyStrategyFactory> HttpProxyStrategyFactory::
                CreateExperimentalHttpProxyStrategyFactory(
                    const String &username,
                    const String &password,
                    Allocator *allocator)
            {

                struct aws_http_proxy_strategy_factory_tunneling_adaptive_test_options config;
                AWS_ZERO_STRUCT(config);
                config.user_name = aws_byte_cursor_from_c_str(username.c_str());
                config.password = aws_byte_cursor_from_c_str(password.c_str());

                struct aws_http_proxy_strategy_factory *factory =
                    aws_http_proxy_strategy_factory_new_tunneling_adaptive_test(allocator, &config);
                if (factory == NULL)
                {
                    return NULL;
                }

                return Aws::Crt::MakeShared<HttpProxyStrategyFactory>(allocator, factory);
            }

            HttpProxyStrategyFactory::HttpProxyStrategyFactory(struct aws_http_proxy_strategy_factory *factory)
                : m_factory(factory)
            {
            }
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
