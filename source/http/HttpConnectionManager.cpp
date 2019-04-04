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
#include <algorithm>
#include <aws/crt/http/HttpConnectionManager.h>

namespace Aws
{
    namespace Crt
    {
        namespace Http
        {
            HttpClientConnectionManagerOptions::HttpClientConnectionManagerOptions()
                : bootstrap(nullptr), initialWindowSize(SIZE_MAX), port(0), max_connections(2)
            {
                AWS_ZERO_STRUCT(socketOptions);
                AWS_ZERO_STRUCT(hostName);
            }

            HttpClientConnectionManager::HttpClientConnectionManager(
                const HttpClientConnectionManagerOptions &connectionManagerOptions,
                Allocator *allocator) noexcept
                : m_allocator(allocator), m_good(true), m_lastError(AWS_ERROR_SUCCESS),
                  m_outstandingVendedConnections(0), m_pendingConnections(0)
            {
                m_initialWindowSize = connectionManagerOptions.initialWindowSize;
                m_bootstrap = connectionManagerOptions.bootstrap;
                assert(connectionManagerOptions.hostName.ptr && connectionManagerOptions.hostName.len);
                m_hostName =
                    String((const char *)connectionManagerOptions.hostName.ptr, connectionManagerOptions.hostName.len);
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

            HttpClientConnectionManager::~HttpClientConnectionManager()
            {
                Vector<std::shared_ptr<HttpClientConnection>> connectionsCopy = m_connections;
                /* make sure all connections we know about are closed. */
                for (auto &connection : connectionsCopy)
                {
                    connection->Close();
                }

                {
                    /* wait for the last one to shutdown so the memory will be cleaned up. */
                    std::unique_lock<std::mutex> m_semaphoreLock(m_connectionsLock);
                    m_shutdownSemaphore.wait(m_semaphoreLock, [this] { return m_connections.empty(); });
                }

                /*
                 * This would be really screwy if this ever happened. I don't even know what error to report, but in
                 * case someone is waiting for a connection, AT LEAST let them know so they can not deadlock.
                 */
                for (auto &pendingConnectionRequest : m_pendingConnectionRequests)
                {
                    pendingConnectionRequest(nullptr, AWS_OP_ERR);
                }
            }

            /* Asssumption: Whoever calls this already holds the lock. */
            bool HttpClientConnectionManager::s_createConnection() noexcept
            {
                if (m_connections.size() + m_outstandingVendedConnections + m_pendingConnections < m_max_size)
                {
                    HttpClientConnectionOptions connectionOptions;
                    connectionOptions.socketOptions = &m_socketOptions;
                    connectionOptions.port = m_port;
                    connectionOptions.hostName = ByteCursorFromCString(m_hostName.c_str());
                    connectionOptions.bootstrap = m_bootstrap;
                    connectionOptions.initialWindowSize = m_initialWindowSize;
                    connectionOptions.allocator = m_allocator;
                    connectionOptions.onConnectionSetup =
                        [this](const std::shared_ptr<HttpClientConnection> &connection, int errorCode) {
                            s_onConnectionSetup(connection, errorCode);
                        };
                    connectionOptions.onConnectionShutdown = [this](HttpClientConnection &connection, int errorCode) {
                        s_onConnectionShutdown(connection, errorCode);
                    };

                    if (m_tlsConnOptions)
                    {
                        connectionOptions.tlsConnOptions = &m_tlsConnOptions;
                    }

                    if (HttpClientConnection::CreateConnection(connectionOptions))
                    {
                        ++m_pendingConnections;
                        return true;
                    }
                    return false;
                }

                return true;
            }

            /* User wants to acquire a connection from the pool, there's a few cases:
             *
             * 1.) We already have a free connection, so return it to the user immediately. We don't care about the
             *      queued connection requests, since it is impossible for there to be a pending connection acquisition
             *      AND a free connection.
             *
             * 2.) We don't have a free connection AND we have not exceeded the pool size limits. Queue the request and
             * wait for the connection setup callback to fire. That callback will invoke the pending request.
             *
             * 3.) We don't have a free connection AND the pool size has been reached. Queue the request, a connection
             * release will pop the queue and invoke the user's callback with a connection.
             */
            bool HttpClientConnectionManager::AcquireConnection(
                const OnClientConnectionAvailable &onClientConnectionAvailable) noexcept
            {
                /** Locking logic:
                 *  We can't hold the lock during the callback since a user may call back into the connection manager
                 *  from the callbacks. We don't use a scoped lock because we need finer grain control.
                 *  All unlocks, should either be followed immediately by a callback and then a return, or
                 *  a return only. If you see anything else, it's a bug.
                 */
                m_connectionsLock.lock();

                /* Case 1 */
                if (!m_connections.empty())
                {
                    auto connection = m_connections.back();
                    m_connections.pop_back();
                    ++m_outstandingVendedConnections;
                    m_connectionsLock.unlock();
                    onClientConnectionAvailable(connection, AWS_ERROR_SUCCESS);
                    return true;
                }

                /* case 2 or 3, we don't know yet. */
                m_pendingConnectionRequests.push_back(onClientConnectionAvailable);

                /* if we have available space, case 2, otherwise case 3 */
                if (!s_createConnection())
                {
                    /* case 2 and the connection creation failed, pop back, because in this rare case, we want to just
                     * tell the caller that our entire attempt at this failed. */
                    m_pendingConnectionRequests.pop_back();
                    m_lastError = aws_last_error();
                    m_connectionsLock.unlock();
                    onClientConnectionAvailable(nullptr, m_lastError);
                    return false;
                }

                m_connectionsLock.unlock();
                return true;
            }

            /*
             * User is finished with a connection and wants to return it to the pool.
             *
             * Case 1, The connection is actually dead and can't be returned to the pool.
             *      Case 1.1 We don't have any pending connection acquisition requests so just let it hit the floor, it
             * will get created in the next Acquire call. Case 1.2 We have pending connection acquisition requests, so
             * create a new connection to replace it, let the callback's fire to notify the user and grow the pool as
             * normal.
             *
             * Case 2, the connection is still good.
             *      Case 2.1 We have pending acquisitions. Just give it to the top of the queue.
             *      Case 2.2 We don't have pending acquisitions, push it to the pool.
             */
            void HttpClientConnectionManager::s_poolOrVendConnection(
                std::shared_ptr<HttpClientConnection> connection,
                bool isRelease) noexcept
            {
                /** Locking logic:
                 *  We can't hold the lock during the callback since a user may call back into the connection manager
                 *  from the callbacks. We don't use a scoped lock because we need finer grain control.
                 *  All unlocks, should either be followed immediately by a callback and then a return, or
                 *  a return only. If you see anything else, it's a bug.
                 */
                m_connectionsLock.lock();
                if (isRelease)
                {
                    --m_outstandingVendedConnections;
                }
                else
                {
                    --m_pendingConnections;
                }

                /* Case 1 */
                if (!*connection)
                {
                    /* Case 1.2 */
                    if (!m_pendingConnectionRequests.empty())
                    {
                        if (!s_createConnection())
                        {
                            auto onClientConnectionAvailable = m_pendingConnectionRequests.front();
                            m_pendingConnectionRequests.pop_front();
                            m_lastError = aws_last_error();
                            m_connectionsLock.unlock();
                            onClientConnectionAvailable(nullptr, m_lastError);
                            return;
                        }
                    }
                    /* Case 1.1 */
                    m_connectionsLock.unlock();
                    return;
                }

                /* Case 2.1*/
                if (!m_pendingConnectionRequests.empty())
                {
                    auto pendingConnectionRequest = m_pendingConnectionRequests.front();
                    m_pendingConnectionRequests.pop_front();
                    ++m_outstandingVendedConnections;
                    m_connectionsLock.unlock();
                    pendingConnectionRequest(std::move(connection), AWS_ERROR_SUCCESS);
                    return;
                }

                /* Case 2.2 */
                m_connections.push_back(std::move(connection));
                m_connectionsLock.unlock();
            }

            void HttpClientConnectionManager::ReleaseConnection(
                std::shared_ptr<HttpClientConnection> connection) noexcept
            {
                s_poolOrVendConnection(connection, true);
            }

            void HttpClientConnectionManager::s_onConnectionSetup(
                const std::shared_ptr<HttpClientConnection> &connection,
                int errorCode) noexcept
            {
                if (!errorCode && connection)
                {
                    s_poolOrVendConnection(connection, false);
                    return;
                }

                m_connectionsLock.lock();
                if (!m_pendingConnectionRequests.empty())
                {
                    auto pendingConnectionRequest = m_pendingConnectionRequests.front();
                    m_pendingConnectionRequests.pop_front();
                    m_connectionsLock.unlock();
                    pendingConnectionRequest(nullptr, errorCode);
                    return;
                }

                m_connectionsLock.unlock();
            }

            void HttpClientConnectionManager::s_onConnectionShutdown(HttpClientConnection &connection, int) noexcept
            {
                {
                    std::lock_guard<std::mutex> connectionsLock(m_connectionsLock);

                    if (!m_connections.empty())
                    {
                        auto toRemove = std::remove_if(
                            m_connections.begin(), m_connections.end(), [&](std::shared_ptr<HttpClientConnection> val) {
                                return val.get() == &connection;
                            });

                        if (toRemove != m_connections.end())
                        {
                            m_connections.erase(toRemove);
                        }
                    }
                }
                /* This is here for the destructor so we don't leak memory and break RAII. */
                m_shutdownSemaphore.notify_one();
            }
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
