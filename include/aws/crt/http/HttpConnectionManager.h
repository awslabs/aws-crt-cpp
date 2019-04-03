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
#include <mutex>

namespace Aws
{
    namespace Crt
    {
        namespace Http
        {
            using OnClientConnectionAvailable =
            std::function<void(std::shared_ptr<HttpClientConnection> connection, int errorCode)>;

            struct HttpConnectionManagerOptions
            {
                Io::ClientBootstrap *bootstrap;
                size_t initialWindowSize;
                Io::SocketOptions *socketOptions;
                Io::TlsConnectionOptions *tlsConnectionOptions;
                ByteCursor hostName;
                uint16_t port;
                size_t max_connections;
            };

            class HttpConnectionManager final
            {
            public:
                HttpConnectionManager(const HttpConnectionManagerOptions& connectionManagerOptions, Allocator *allocator = DefaultAllocator()) noexcept;
                ~HttpConnectionManager();

                bool AcquireConnection(const OnClientConnectionAvailable & onClientConnectionAvailable);
                void ReleaseConnection(std::shared_ptr<HttpClientConnection> connection);

            private:
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
                size_t m_max_size;
                std::mutex m_connectionsLock;

                void s_onConnectionSetup(const std::shared_ptr<HttpClientConnection> &connection, int errorCode);
                void s_onConnectionShutdown(HttpClientConnection &connection, int errorCode);
            };
        }
    }
}
