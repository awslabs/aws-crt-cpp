#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Types.h>
#include <memory>
#include <aws/http/proxy_strategy.h>

 
/*SA-Added Start*/
typedef void (*proxy_callback_send_t)(size_t data_length, uint8_t *data);
typedef char *(*proxy_callback_get_t)(int callback_state);
static proxy_callback_send_t mycallback_1;
static proxy_callback_get_t mycallback_2;
/*SA-Added End*/
  
namespace Aws
{
    namespace Crt
    {
        namespace Http
        {
         
             

            /*SA-Added Start*/
            /*
            class HttpProxyStrategyCallback
            {
                proxy_callback_send_t mycallback_1;
                proxy_callback_get_t mycallback_2;
                
                void _sendDataCallbackUser(size_t data_length, uint8_t *data);
                static void _sendDataCallback(size_t data_length, uint8_t *data, void *user);

                char *_getDataCallbackUser(int callback_state);
                static char *_getDataCallback(int callback_state, void *user);

              public:
               
                HttpProxyStrategyCallback(proxy_callback_send_t callback_A, proxy_callback_get_t callback_B);
                
            };
            */
            /*SA-Added End*/
            class HttpProxyStrategyFactory
            {
              
            public:
               
                void _sendDataCallbackUser(size_t data_length, uint8_t *data);
                static void _sendDataCallback(size_t data_length, uint8_t *data, void *user);

                char *_getDataCallbackUser(int callback_state);
                static char *_getDataCallback(int callback_state, void *user); 

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

                std::shared_ptr<HttpProxyStrategyFactory> CreateAdaptiveKerberosNtlmHttpProxyStrategyFactory(
                    proxy_callback_send_t callback_1,
                    proxy_callback_get_t callback_2,
                    Allocator *allocator = g_allocator);

                /*SA-Added Start*/
                static std::shared_ptr<HttpProxyStrategyFactory> CreateKerberosHttpProxyStrategyFactory(
                    enum aws_http_proxy_connection_type connectionType,
                    const String &usertoken,
                    Allocator *allocator = g_allocator);

                std::shared_ptr<HttpProxyStrategyFactory> CreateAdaptiveNtlmHttpProxyStrategyFactory(
                    proxy_callback_send_t callback_1,
                    proxy_callback_get_t callback_2,
                    Allocator *allocator = g_allocator);

                /*SA-Added End*/
              private:
                struct aws_http_proxy_strategy_factory *m_factory;
                
            };
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
