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
                std::promise<void> ShutdownPromise;
                OnClientBootstrapShutdownComplete ShutdownCallback;

                static void OnShutdownComplete(void *userData)
                {
                    auto callbackData = static_cast<ClientBootstrapCallbackData *>(userData);

                    callbackData->ShutdownPromise.set_value();
                    callbackData->ShutdownCallback();

                    delete callbackData;
                }
            };

            ClientBootstrap::ClientBootstrap(
                EventLoopGroup &elGroup,
                HostResolver &resolver,
                Allocator *allocator) noexcept
                : m_bootstrap(nullptr), m_lastError(AWS_ERROR_SUCCESS),
                  m_callbackData(new ClientBootstrapCallbackData()), m_enableBlockingShutdown(false)
            {
                m_shutdownFuture = m_callbackData->ShutdownPromise.get_future();

                aws_client_bootstrap_options options;
                options.event_loop_group = elGroup.GetUnderlyingHandle();
                options.host_resolution_config = resolver.GetConfig();
                options.host_resolver = resolver.GetUnderlyingHandle();
                options.on_shutdown_complete = ClientBootstrapCallbackData::OnShutdownComplete;
                options.user_data = m_callbackData.get();
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
                    // Release m_callbackData, it destroys itself when shutdown completes.
                    m_callbackData.release();

                    aws_client_bootstrap_release(m_bootstrap);
                    if (m_enableBlockingShutdown)
                    {
                        // If your program is stuck here, stop using EnableBlockingShutdown()
                        m_shutdownFuture.wait();
                    }
                }
            }

            ClientBootstrap::operator bool() const noexcept { return m_lastError == AWS_ERROR_SUCCESS; }

            int ClientBootstrap::LastError() const noexcept { return m_lastError; }

            void ClientBootstrap::SetShutdownCompleteCallback(OnClientBootstrapShutdownComplete callback)
            {
                m_callbackData->ShutdownCallback = std::move(callback);
            }

            void ClientBootstrap::EnableBlockingShutdown() noexcept { m_enableBlockingShutdown = true; }

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
