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
#include <aws/crt/http/HttpConnectionManager.h>

#include <algorithm>
#include <aws/http/connection_manager.h>

namespace Aws
{
    namespace Crt
    {
        namespace Http
        {
            struct ConnectionManagerCallbackArgs
            {
                ConnectionManagerCallbackArgs() = default;
                OnClientConnectionAvailable m_onClientConnectionAvailable;
                std::shared_ptr<HttpClientConnectionManager> m_connectionManager;
            };

            HttpClientConnectionManagerOptions::HttpClientConnectionManagerOptions() noexcept
                : m_connectionOptions(), m_maxConnections(1)
            {
            }

            std::shared_ptr<HttpClientConnectionManager> HttpClientConnectionManager::NewClientConnectionManager(
                const HttpClientConnectionManagerOptions &connectionManagerOptions,
                Allocator *allocator) noexcept
            {
                auto *toSeat = static_cast<HttpClientConnectionManager *>(
                    aws_mem_acquire(allocator, sizeof(HttpClientConnectionManager)));
                if (toSeat)
                {
                    toSeat = new (toSeat) HttpClientConnectionManager(connectionManagerOptions, allocator);
                    return std::shared_ptr<HttpClientConnectionManager>(
                        toSeat, [allocator](HttpClientConnectionManager *manager) { Delete(manager, allocator); });
                }

                return nullptr;
            }

            HttpClientConnectionManager::HttpClientConnectionManager(
                const HttpClientConnectionManagerOptions &options,
                Allocator *allocator) noexcept
                : m_allocator(allocator), m_connectionManager(nullptr), m_options(options)
            {
                const auto &connectionOptions = m_options.GetConnectionOptions();
                AWS_FATAL_ASSERT(connectionOptions.GetHostName().size() > 0);
                AWS_FATAL_ASSERT(connectionOptions.GetPort() > 0);

                aws_http_connection_manager_options managerOptions;
                AWS_ZERO_STRUCT(managerOptions);
                managerOptions.bootstrap = connectionOptions.GetBootstrap()->GetUnderlyingHandle();
                managerOptions.port = connectionOptions.GetPort();
                managerOptions.max_connections = m_options.GetMaxConnections();
                managerOptions.socket_options = &connectionOptions.GetSocketOptions().GetImpl();
                managerOptions.initial_window_size = connectionOptions.GetInitialWindowSize();

                aws_http_proxy_options proxyOptions;
                AWS_ZERO_STRUCT(proxyOptions);
                if (connectionOptions.GetProxyOptions())
                {
                    const auto &proxyOpts = connectionOptions.GetProxyOptions();
                    proxyOptions.host = aws_byte_cursor_from_c_str(proxyOpts->GetHostName().c_str());
                    proxyOptions.port = proxyOpts->GetPort();
                    proxyOptions.auth_type =
                        (enum aws_http_proxy_authentication_type)proxyOpts->GetAuthenticationType();

                    if (proxyOpts->GetAuthenticationType() == AwsHttpProxyAuthenticationType::Basic)
                    {
                        proxyOptions.auth_username =
                            aws_byte_cursor_from_c_str(proxyOpts->GetBasicAuthUsername().c_str());
                        proxyOptions.auth_password =
                            aws_byte_cursor_from_c_str(proxyOpts->GetBasicAuthPassword().c_str());
                    }

                    if (proxyOpts->GetTlsOptions())
                    {
                        proxyOptions.tls_options =
                            const_cast<aws_tls_connection_options *>(proxyOpts->GetTlsOptions()->GetUnderlyingHandle());
                    }

                    managerOptions.proxy_options = &proxyOptions;
                }

                if (connectionOptions.GetTlsOptions())
                {
                    managerOptions.tls_connection_options = const_cast<aws_tls_connection_options *>(
                        connectionOptions.GetTlsOptions()->GetUnderlyingHandle());
                }
                managerOptions.host = aws_byte_cursor_from_c_str(connectionOptions.GetHostName().c_str());

                m_connectionManager = aws_http_connection_manager_new(allocator, &managerOptions);
            }

            HttpClientConnectionManager::~HttpClientConnectionManager()
            {
                if (m_connectionManager)
                {
                    aws_http_connection_manager_release(m_connectionManager);
                    m_connectionManager = nullptr;
                }
            }

            bool HttpClientConnectionManager::AcquireConnection(
                const OnClientConnectionAvailable &onClientConnectionAvailable) noexcept
            {

                auto connectionManagerCallbackArgs = Aws::Crt::New<ConnectionManagerCallbackArgs>(m_allocator);
                if (!connectionManagerCallbackArgs)
                {
                    return false;
                }

                connectionManagerCallbackArgs->m_connectionManager = shared_from_this();
                connectionManagerCallbackArgs->m_onClientConnectionAvailable = onClientConnectionAvailable;

                aws_http_connection_manager_acquire_connection(
                    m_connectionManager, s_onConnectionSetup, connectionManagerCallbackArgs);
                return true;
            }

            class ManagedConnection final : public HttpClientConnection
            {
              public:
                ManagedConnection(
                    aws_http_connection *connection,
                    std::shared_ptr<HttpClientConnectionManager> connectionManager)
                    : HttpClientConnection(connection, connectionManager->m_allocator),
                      m_connectionManager(std::move(connectionManager))
                {
                }

                ~ManagedConnection() override
                {
                    if (m_connection)
                    {
                        aws_http_connection_manager_release_connection(
                            m_connectionManager->m_connectionManager, m_connection);
                        m_connection = nullptr;
                    }
                }

              private:
                std::shared_ptr<HttpClientConnectionManager> m_connectionManager;
            };

            void HttpClientConnectionManager::s_onConnectionSetup(
                aws_http_connection *connection,
                int errorCode,
                void *userData) noexcept
            {
                auto callbackArgs = static_cast<ConnectionManagerCallbackArgs *>(userData);
                auto manager = std::move(callbackArgs->m_connectionManager);
                auto callback = std::move(callbackArgs->m_onClientConnectionAvailable);

                Delete(callbackArgs, manager->m_allocator);

                if (errorCode)
                {
                    callback(nullptr, errorCode);
                    return;
                }

                auto connectionObj = std::allocate_shared<ManagedConnection>(
                    Aws::Crt::StlAllocator<ManagedConnection>(), connection, manager);

                if (!connectionObj)
                {
                    callback(nullptr, AWS_ERROR_OOM);
                    return;
                }

                callback(connectionObj, AWS_OP_SUCCESS);
            }

        } // namespace Http
    }     // namespace Crt
} // namespace Aws
