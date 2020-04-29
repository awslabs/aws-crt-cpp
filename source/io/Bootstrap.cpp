/*
 * Copyright 2010-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
#include <aws/crt/io/Bootstrap.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {

            // Holds the bootstrap's shutdown promise.
            // Lives until the bootstrap's shutdown-complete callback fires.
            struct ClientBootstrapCallbackData
            {
                Allocator *allocator;
                std::promise<void> shutdownPromise;

                static void onShutdownComplete(void *userData)
                {
                    auto callbackData = static_cast<ClientBootstrapCallbackData *>(userData);
                    callbackData->shutdownPromise.set_value();
                    Delete(callbackData, callbackData->allocator);
                }
            };

            ClientBootstrap::ClientBootstrap(
                EventLoopGroup &elGroup,
                HostResolver &resolver,
                Allocator *allocator) noexcept
                : m_bootstrap(nullptr), m_callbackData(nullptr), m_lastError(AWS_ERROR_SUCCESS)
            {
                m_callbackData = New<ClientBootstrapCallbackData>(allocator);
                if (!m_callbackData)
                {
                    m_lastError = aws_last_error();
                    return;
                }
                m_callbackData->allocator = allocator;

                aws_client_bootstrap_options options;
                options.event_loop_group = elGroup.GetUnderlyingHandle();
                options.host_resolution_config = resolver.GetConfig();
                options.host_resolver = resolver.GetUnderlyingHandle();
                options.on_shutdown_complete = ClientBootstrapCallbackData::onShutdownComplete;
                options.user_data = m_callbackData;
                m_bootstrap = aws_client_bootstrap_new(allocator, &options);
                if (!m_bootstrap)
                {
                    m_lastError = aws_last_error();
                }
            }

            ClientBootstrap::~ClientBootstrap()
            {
                if (m_bootstrap)
                {
                    aws_client_bootstrap_release(m_bootstrap);
                    m_bootstrap = nullptr;
                    m_lastError = AWS_ERROR_UNKNOWN;
                }
                else if (m_callbackData)
                {
                    // Normally, m_callbackData waits until the shutdown-complete callback to destroy itself.
                    // But if m_bootstrap failed creation, shutdown will never fire, so manually destroy m_callbackData.
                    Delete(m_callbackData, m_callbackData->allocator);
                }
            }

            ClientBootstrap::operator bool() const noexcept { return m_lastError == AWS_ERROR_SUCCESS; }

            int ClientBootstrap::LastError() const noexcept { return m_lastError; }

            std::future<void> ClientBootstrap::GetShutdownFuture()
            {
                return m_callbackData->shutdownPromise.get_future();
            }

            aws_client_bootstrap *ClientBootstrap::GetUnderlyingHandle() const noexcept
            {
                if (*this)
                {
                    return m_bootstrap;
                }

                return nullptr;
            }
        } // namespace Io
    }     // namespace Crt
} // namespace Aws
