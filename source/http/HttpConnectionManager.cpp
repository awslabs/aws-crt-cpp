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

namespace Aws
{
    namespace Crt
    {
        namespace Http
        {
            HttpConnectionManager::HttpConnectionManager(const HttpConnectionManagerOptions& connectionManagerOptions, Allocator *allocator) noexcept :
                m_allocator(allocator), m_good(true), m_lastError(AWS_ERROR_SUCCESS)
            {
                m_initialWindowSize = connectionManagerOptions.initialWindowSize;
                m_bootstrap = connectionManagerOptions.bootstrap;
                m_hostName = String((const char *)connectionManagerOptions.hostName.ptr, connectionManagerOptions.hostName.len);
                m_max_size = connectionManagerOptions.max_connections;
                m_port = connectionManagerOptions.port;
                m_socketOptions = *connectionManagerOptions.socketOptions;

                if (connectionManagerOptions.tlsConnectionOptions)
                {
                    m_tlsConnOptions = *connectionManagerOptions.tlsConnectionOptions;

                    if (!m_tlsConnOptions)
                    {
                        m_lastError = aws_last_error();
                        m_good = false;
                    }
                }
            }

            HttpConnectionManager::~HttpConnectionManager()
            {
                Vector<std::shared_ptr<HttpClientConnection>> connectionsCopy = m_connections;
                for (auto& connection : connectionsCopy)
                {
                    connection->Close();
                }
            }

            bool HttpConnectionManager::AcquireConnection(const OnClientConnectionAvailable & onClientConnectionAvailable)
            {
                std::lock_guard<std::mutex> connectionsLock(m_connectionsLock);

                if (!m_connections.empty())
                {
                    auto connection = m_connections.back();
                    m_connections.pop_back();
                    onClientConnectionAvailable(connection, AWS_ERROR_SUCCESS);
                    return true;
                }

                m_pendingConnectionRequests.push_back(onClientConnectionAvailable);

                if (m_connections.size() < m_max_size)
                {
                    HttpClientConnectionOptions connectionOptions;
                    connectionOptions.socketOptions = &m_socketOptions;
                    connectionOptions.port = m_port;
                    connectionOptions.hostName = ByteCursorFromCString(m_hostName.c_str());
                    connectionOptions.bootstrap = m_bootstrap;
                    connectionOptions.initialWindowSize = m_initialWindowSize;
                    connectionOptions.allocator = m_allocator;
                    connectionOptions.onConnectionSetup = [this](const std::shared_ptr<HttpClientConnection> &connection, int errorCode)
                    {
                        s_onConnectionSetup(connection, errorCode);
                    };
                    connectionOptions.onConnectionShutdown = [this](HttpClientConnection &connection, int errorCode)
                    {
                        s_onConnectionShutdown(connection, errorCode);
                    };

                    if (m_tlsConnOptions) {
                        connectionOptions.tlsConnOptions = &m_tlsConnOptions;
                    }

                    if (!HttpClientConnection::CreateConnection(connectionOptions))
                    {
                        m_pendingConnectionRequests.pop_back();
                        m_lastError = aws_last_error();
                        onClientConnectionAvailable(nullptr, m_lastError);
                        return false;
                    }
                }

                return true;
            }

            void HttpConnectionManager::ReleaseConnection(std::shared_ptr<HttpClientConnection> connection)
            {
                std::lock_guard<std::mutex> connectionsLock(m_connectionsLock);

                if (!m_pendingConnectionRequests.empty())
                {
                    auto pendingConnectionRequest = m_pendingConnectionRequests.front();
                    m_pendingConnectionRequests.pop_front();
                    pendingConnectionRequest(std::move(connection), AWS_ERROR_SUCCESS);
                    return;
                }

                m_connections.push_back(std::move(connection));
            }

            void HttpConnectionManager::s_onConnectionSetup(const std::shared_ptr<HttpClientConnection> &connection, int errorCode)
            {
                if (!errorCode && connection)
                {
                    ReleaseConnection(connection);
                    return;
                }

                std::lock_guard<std::mutex> connectionsLock(m_connectionsLock);
                if (!m_pendingConnectionRequests.empty())
                {
                    auto pendingConnectionRequest = m_pendingConnectionRequests.front();
                    m_pendingConnectionRequests.pop_front();
                    pendingConnectionRequest(nullptr, errorCode);
                }

            }

            void HttpConnectionManager::s_onConnectionShutdown(HttpClientConnection &connection, int)
            {
                std::lock_guard<std::mutex> connectionsLock(m_connectionsLock);

                m_connections.erase(std::remove_if(m_connections.begin(), m_connections.end(),
                        [&](std::shared_ptr<HttpClientConnection> val){ return val.get() == &connection; }));
            }
        }
    }
}
