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
            /*SA-Added Start*/
            /*
            char *HttpProxyStrategyCallback::_getDataCallbackUser(int callback_state)
            {
                char *user_data = (mycallback_2)(callback_state);
                return user_data;
            }

            char *HttpProxyStrategyCallback::_getDataCallback(int callback_state, void *user)
            {
                HttpProxyStrategyCallback *mySelf = (HttpProxyStrategyCallback *)user;
                char *user_data = mySelf->_getDataCallbackUser(callback_state);
                return user_data;
            }
            
            void HttpProxyStrategyCallback::_sendDataCallbackUser(size_t data_length, uint8_t *data) 
            { 
                (mycallback_1)(data_length, data); 
            }

            void HttpProxyStrategyCallback::_sendDataCallback(size_t data_length, uint8_t *data, void *user)
            {
                HttpProxyStrategyCallback *mySelf = (HttpProxyStrategyCallback *)user;

                mySelf->_sendDataCallbackUser(data_length, data); 
            }
            
            HttpProxyStrategyCallback::HttpProxyStrategyCallback(proxy_callback_send_t callback_1,proxy_callback_get_t callback_2)
            {
                mycallback_1 = callback_1;
                mycallback_2 = callback_2;
                aws_http_proxy_connection_configure_callback(_sendDataCallback, _getDataCallback, this);
            }
            */
            /*SA-Added End*/   

            /*SA-Added Start*/
            char *HttpProxyStrategyFactory::_getDataCallbackUser(int callback_state)
            {
                char *user_data = (mycallback_2)(callback_state);
                return user_data;
            }

            char *HttpProxyStrategyFactory::_getDataCallback(int callback_state, void *user)
            {
                HttpProxyStrategyFactory *mySelf = (HttpProxyStrategyFactory *)user;
                char *user_data = mySelf->_getDataCallbackUser(callback_state);
                return user_data;
            }

            void HttpProxyStrategyFactory::_sendDataCallbackUser(size_t data_length, uint8_t *data)
            {
                (mycallback_1)(data_length, data);
            }

            void HttpProxyStrategyFactory::_sendDataCallback(size_t data_length, uint8_t *data, void *user)
            {
                
                HttpProxyStrategyFactory *mySelf = (HttpProxyStrategyFactory *)user;
                mySelf->_sendDataCallbackUser(data_length, data);
            }
            /*SA-Added End*/        

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

            std::shared_ptr<HttpProxyStrategyFactory> HttpProxyStrategyFactory::
                CreateAdaptiveKerberosNtlmHttpProxyStrategyFactory(
                    proxy_callback_send_t callback_1,
                    proxy_callback_get_t callback_2,
                    Allocator *allocator)
            {

                struct aws_http_proxy_strategy_factory_tunneling_adaptive_kerberos_options kerberos_config;
                struct aws_http_proxy_strategy_factory_tunneling_adaptive_ntlm_options ntlm_config;

                AWS_ZERO_STRUCT(kerberos_config);
                AWS_ZERO_STRUCT(ntlm_config);

                mycallback_1 = callback_1;
                mycallback_2 = callback_2;
                kerberos_config.kerberos_options.func_1 = _sendDataCallback;
                kerberos_config.kerberos_options.func_2 = _getDataCallback;
                kerberos_config.kerberos_options.userData = this;
                ntlm_config.ntlm_options.func_1 = _sendDataCallback;
                ntlm_config.ntlm_options.func_2 = _getDataCallback;
                ntlm_config.ntlm_options.userData = this;

                struct aws_http_proxy_strategy_factory *factory =
                    aws_http_proxy_strategy_factory_new_tunneling_adaptive_kerberos_ntlm(
                        allocator, &kerberos_config, &ntlm_config);
                if (factory == NULL)
                {
                    return NULL;
                }

                return Aws::Crt::MakeShared<HttpProxyStrategyFactory>(allocator, factory);
            }
            /*SA-Added Start*/
            std::shared_ptr<HttpProxyStrategyFactory> HttpProxyStrategyFactory::CreateKerberosHttpProxyStrategyFactory(
                enum aws_http_proxy_connection_type connectionType,
                const String &usertoken,
                Allocator *allocator)
            {
                struct aws_http_proxy_strategy_factory_kerberos_auth_config config;
                AWS_ZERO_STRUCT(config);
                config.proxy_connection_type = connectionType;
                config.user_token = aws_byte_cursor_from_c_str(usertoken.c_str());

                struct aws_http_proxy_strategy_factory *factory =
                    aws_http_proxy_strategy_factory_new_kerberos_auth(allocator, &config);
                if (factory == NULL)
                {
                    return NULL;
                }

                return Aws::Crt::MakeShared<HttpProxyStrategyFactory>(allocator, factory);
            }

            std::shared_ptr<HttpProxyStrategyFactory> HttpProxyStrategyFactory::
                CreateAdaptiveNtlmHttpProxyStrategyFactory(
                    proxy_callback_send_t callback_1,
                    proxy_callback_get_t callback_2,
                    Allocator *allocator)

            {

                struct aws_http_proxy_strategy_factory_tunneling_adaptive_ntlm_options config;
                AWS_ZERO_STRUCT(config);
               
                mycallback_1 = callback_1;
                mycallback_2 = callback_2;
                config.ntlm_options.func_1 = _sendDataCallback;
                config.ntlm_options.func_2 = _getDataCallback;
                config.ntlm_options.userData = this;
                             
                struct aws_http_proxy_strategy_factory *factory =
                    aws_http_proxy_strategy_factory_new_tunneling_adaptive_ntlm(allocator, &config);
                if (factory == NULL)
                {
                    return NULL;
                }

                return Aws::Crt::MakeShared<HttpProxyStrategyFactory>(allocator, factory);
            }
            /*SA-Added End*/
            HttpProxyStrategyFactory::HttpProxyStrategyFactory(struct aws_http_proxy_strategy_factory *factory)
                : m_factory(factory)
            {
            }
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
