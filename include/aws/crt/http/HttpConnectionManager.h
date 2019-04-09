#pragma once
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
#include <aws/crt/http/HttpConnection.h>
#include <condition_variable>
#include <mutex>

namespace Aws
{
    namespace Crt
    {
        namespace Http
        {
            /**
             * Invoked when a connection from the pool is available. If a connection was successfully obtained
             * the connection shared_ptr can be seated into your own copy of connection. If it failed, errorCode
             * will be non-zero. It is your responsibility to release the connection when you are finished with it.
             */
            using OnClientConnectionAvailable =
                std::function<void(std::shared_ptr<HttpClientConnection> connection, int errorCode)>;

            struct HttpClientConnectionManagerOptions
            {
                HttpClientConnectionManagerOptions();
                Io::ClientBootstrap *bootstrap;
                size_t initialWindowSize;
                Io::SocketOptions *socketOptions;
                Io::TlsConnectionOptions *tlsConnectionOptions;
                ByteCursor hostName;
                uint16_t port;
                size_t maxConnections;
            };

            /**
             * Manages a pool of connections to a specific endpoint using the same socket and tls options.
             */
            class HttpClientConnectionManager final : public std::enable_shared_from_this<HttpClientConnectionManager>
            {
              public:
                ~HttpClientConnectionManager();

                /**
                 * Acquires a connection from the pool. onClientConnectionAvailable will be invoked upon an available
                 * connection. Returns true if the connection request was successfully pooled, returns false if it
                 * failed. On failure, onClientConnectionAvailable will not be invoked. After receiving a connection,
                 * you must invoke ReleaseConnection().
                 */
                bool AcquireConnection(const OnClientConnectionAvailable &onClientConnectionAvailable) noexcept;

                /**
                 * Releases a connection back to the pool. This will cause queued consumers to be serviced, or the
                 * connection will be pooled waiting on another call to AcquireConnection
                 */
                void ReleaseConnection(std::shared_ptr<HttpClientConnection> connection) noexcept;

                int LastError() const noexcept { return m_lastError; }
                explicit operator bool() const noexcept { return m_good; }

                static std::shared_ptr<HttpClientConnectionManager> NewClientConnectionManager(
                    const HttpClientConnectionManagerOptions &connectionManagerOptions,
                    Allocator *allocator = DefaultAllocator()) noexcept;

              private:
                HttpClientConnectionManager(
                    const HttpClientConnectionManagerOptions &connectionManagerOptions,
                    Allocator *allocator = DefaultAllocator()) noexcept;

                Vector<std::shared_ptr<HttpClientConnection>> m_connections;
                List<OnClientConnectionAvailable> m_pendingConnectionRequests;
                Allocator *m_allocator;
                Io::ClientBootstrap *m_bootstrap;
                size_t m_initialWindowSize;
                Io::SocketOptions m_socketOptions;
                Io::TlsConnectionOptions m_tlsConnOptions;
                String m_hostName;
                uint16_t m_port;
                bool m_good;
                int m_lastError;
                size_t m_maxSize;
                size_t m_outstandingVendedConnections;
                size_t m_pendingConnections;
                std::mutex m_connectionsLock;

                void onConnectionSetup(const std::shared_ptr<HttpClientConnection> &connection, int errorCode) noexcept;
                void onConnectionShutdown(HttpClientConnection &connection, int errorCode) noexcept;
                bool createConnection() noexcept;
                void poolOrVendConnection(std::shared_ptr<HttpClientConnection> connection, bool isRelease) noexcept;
            };
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
