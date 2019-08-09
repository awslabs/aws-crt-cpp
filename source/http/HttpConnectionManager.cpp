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

            HttpClientConnectionManagerOptions::HttpClientConnectionManagerOptions()
                : bootstrap(nullptr), initialWindowSize(SIZE_MAX), port(0), maxConnections(2)
            {
                AWS_ZERO_STRUCT(socketOptions);
                AWS_ZERO_STRUCT(hostName);
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
                const HttpClientConnectionManagerOptions &connectionManagerOptions,
                Allocator *allocator) noexcept
                : m_connectionManager(nullptr), m_allocator(allocator), m_good(true), m_lastError(AWS_ERROR_SUCCESS)
            {
                m_bootstrap = connectionManagerOptions.bootstrap;
                AWS_ASSERT(connectionManagerOptions.hostName.ptr && connectionManagerOptions.hostName.len);

                if (connectionManagerOptions.tlsConnectionOptions)
                {
                    m_tlsConnOptions = *connectionManagerOptions.tlsConnectionOptions;

                    if (!m_tlsConnOptions)
                    {
                        m_lastError = aws_last_error();
                        m_good = false;
                    }
                }

                aws_http_connection_manager_options managerOptions;
                AWS_ZERO_STRUCT(managerOptions);
                managerOptions.bootstrap = m_bootstrap->GetUnderlyingHandle();
                managerOptions.port = connectionManagerOptions.port;
                managerOptions.max_connections = connectionManagerOptions.maxConnections;
                managerOptions.socket_options = connectionManagerOptions.socketOptions;
                managerOptions.initial_window_size = connectionManagerOptions.initialWindowSize;

                if (m_tlsConnOptions)
                {
                    managerOptions.tls_connection_options =
                        const_cast<aws_tls_connection_options *>(m_tlsConnOptions.GetUnderlyingHandle());
                }
                managerOptions.host = *connectionManagerOptions.hostName.Get();

                m_connectionManager = aws_http_connection_manager_new(allocator, &managerOptions);

                if (!m_connectionManager)
                {
                    m_lastError = aws_last_error();
                    m_good = false;
                }
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
                    m_lastError = aws_last_error();
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
