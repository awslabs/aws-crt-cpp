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
#include <aws/crt/io/Bootstrap.h>

namespace Aws
{
    namespace Crt
    {
        namespace Http
        {
            /* this exists so we can keep the shared_ptr for the connection around until the connection has actually
             * shutdown. */
            struct ConnectionWrapper
            {
                explicit ConnectionWrapper(const HttpClient *client) : connection(nullptr), client(client) {}
                std::shared_ptr<HttpConnection> connection;
                const HttpClient *client;
            };

            void HttpClient::s_onClientConnectionSetup(
                struct aws_http_connection *connection,
                int errorCode,
                void *user_data) noexcept
            {
                /**
                 * Allocate an HttpConnection and seat it to `ConnectionWrapper`'s shared_ptr.
                 */
                auto *connectionWrapper = reinterpret_cast<ConnectionWrapper *>(user_data);
                int retError = errorCode;
                if (!errorCode)
                {
                    Allocator *allocator = connectionWrapper->client->m_allocator;

                    auto *toSeat =
                        reinterpret_cast<HttpConnection *>(aws_mem_acquire(allocator, sizeof(HttpConnection)));
                    if (toSeat)
                    {
                        toSeat = new (toSeat) HttpConnection(connection, allocator);
                        connectionWrapper->connection =
                            std::shared_ptr<HttpConnection>(toSeat, [allocator](HttpConnection *connection) {
                                connection->~HttpConnection();
                                aws_mem_release(allocator, reinterpret_cast<void *>(connection));
                            });

                        connectionWrapper->client->onConnectionSetup(connectionWrapper->connection, errorCode);
                        return;
                    }

                    retError = aws_last_error();
                }

                connectionWrapper->client->onConnectionSetup(nullptr, retError);
                connectionWrapper->~ConnectionWrapper();
                aws_mem_release(connectionWrapper->client->m_allocator, reinterpret_cast<void *>(connectionWrapper));
            }

            void HttpClient::s_onClientConnectionShutdown(
                struct aws_http_connection *connection,
                int errorCode,
                void *user_data) noexcept
            {
                (void)connection;
                /* now that we're shutting down, we can release the internal ref count. */
                auto *connectionWrapper = reinterpret_cast<ConnectionWrapper *>(user_data);
                connectionWrapper->client->onConnectionShutdown(connectionWrapper->connection, errorCode);
                connectionWrapper->connection.reset();
                connectionWrapper->~ConnectionWrapper();
                aws_mem_release(connectionWrapper->client->m_allocator, reinterpret_cast<void *>(connectionWrapper));
            }

            HttpClient::HttpClient(
                Io::ClientBootstrap *bootstrap,
                std::size_t initialWindowSize,
                Allocator *allocator) noexcept
                : m_allocator(allocator), m_bootstrap(bootstrap), m_initialWindowSize(initialWindowSize)
            {
            }

            bool HttpClient::NewConnection(
                const ByteCursor &hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                const Io::TlsConnectionOptions &tlsConnOptions) const noexcept
            {
                assert(onConnectionShutdown);
                assert(onConnectionSetup);

                void *connectionWrapperMemory = aws_mem_acquire(m_allocator, sizeof(ConnectionWrapper));
                if (!connectionWrapperMemory)
                {
                    return false;
                }

                auto connectionWrapper = new (connectionWrapperMemory) ConnectionWrapper(this);

                aws_http_client_connection_options options;
                AWS_ZERO_STRUCT(options);
                options.self_size = sizeof(aws_http_client_connection_options);
                options.bootstrap = m_bootstrap->GetUnderlyingHandle();
                options.tls_options = const_cast<aws_tls_connection_options *>(tlsConnOptions.GetUnderlyingHandle());
                options.allocator = m_allocator;
                options.user_data = connectionWrapper;
                options.host_name = hostName;
                options.port = port;
                options.initial_window_size = m_initialWindowSize;
                options.socket_options = &const_cast<Io::SocketOptions &>(socketOptions);
                options.on_setup = HttpClient::s_onClientConnectionSetup;
                options.on_shutdown = HttpClient::s_onClientConnectionShutdown;

                if (aws_http_client_connect(&options))
                {
                    connectionWrapper->~ConnectionWrapper();
                    aws_mem_release(m_allocator, connectionWrapperMemory);
                    return false;
                }

                return true;
            }

            bool HttpClient::NewConnection(
                const ByteCursor &hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions) const noexcept
            {
                void *connectionWrapperMemory = aws_mem_acquire(m_allocator, sizeof(ConnectionWrapper));
                if (!connectionWrapperMemory)
                {
                    return false;
                }

                ConnectionWrapper *connectionWrapper = new (connectionWrapperMemory) ConnectionWrapper(this);

                aws_http_client_connection_options options;
                AWS_ZERO_STRUCT(options);
                options.self_size = sizeof(aws_http_client_connection_options);
                options.bootstrap = m_bootstrap->GetUnderlyingHandle();
                options.tls_options = nullptr;
                options.allocator = m_allocator;
                options.user_data = connectionWrapper;
                options.host_name = hostName;
                options.port = port;
                options.initial_window_size = m_initialWindowSize;
                options.socket_options = &const_cast<Io::SocketOptions &>(socketOptions);
                options.on_setup = HttpClient::s_onClientConnectionSetup;
                options.on_shutdown = HttpClient::s_onClientConnectionShutdown;

                if (aws_http_client_connect(&options))
                {
                    connectionWrapper->~ConnectionWrapper();
                    aws_mem_release(m_allocator, connectionWrapperMemory);
                    return false;
                }

                return true;
            }

            HttpConnection::HttpConnection(aws_http_connection *connection, Allocator *allocator) noexcept
                : m_connection(connection), m_allocator(allocator), m_lastError(AWS_ERROR_SUCCESS)
            {
            }

            HttpConnection::~HttpConnection()
            {
                if (m_connection)
                {
                    aws_http_connection_release(m_connection);
                    m_connection = nullptr;
                }
            }

            struct StreamWrapper
            {
                Allocator *allocator;
                std::shared_ptr<HttpStream> stream;
            };

            std::shared_ptr<HttpStream> HttpConnection::NewStream(const HttpRequestOptions &requestOptions) noexcept
            {
                assert(requestOptions.onIncomingHeaders);
                assert(requestOptions.onStreamComplete);

                aws_http_request_options options;
                AWS_ZERO_STRUCT(options);
                options.self_size = sizeof(aws_http_request_options);
                options.uri = requestOptions.uri;
                options.method_str = requestOptions.method;
                options.header_array = requestOptions.headerArray;
                options.num_headers = requestOptions.headerArrayLength;
                options.stream_outgoing_body = HttpStream::s_onStreamOutgoingBody;
                options.on_response_body = HttpStream::s_onIncomingBody;
                options.on_response_headers = HttpStream::s_onIncomingHeaders;
                options.on_response_header_block_done = HttpStream::s_onIncomingHeaderBlockDone;
                options.on_complete = HttpStream::s_onStreamComplete;
                options.client_connection = m_connection;

                /* Do the same ref counting trick we did with HttpConnection. We need to maintain a reference
                 * internally (regardless of what the user does), until the Stream shuts down. */
                auto *toSeat = reinterpret_cast<HttpStream *>(aws_mem_acquire(m_allocator, sizeof(HttpStream)));

                if (toSeat)
                {
                    auto *wrapper =
                        reinterpret_cast<StreamWrapper *>(aws_mem_acquire(m_allocator, sizeof(StreamWrapper)));
                    if (!wrapper)
                    {
                        aws_mem_release(m_allocator, reinterpret_cast<void *>(toSeat));
                        return nullptr;
                    }

                    toSeat = new (toSeat) HttpStream(this->shared_from_this());
                    wrapper = new (wrapper) StreamWrapper;

                    Allocator *captureAllocator = m_allocator;
                    wrapper->stream = std::shared_ptr<HttpStream>(toSeat, [captureAllocator](HttpStream *stream) {
                        stream->~HttpStream();
                        aws_mem_release(captureAllocator, reinterpret_cast<void *>(stream));
                    });

                    toSeat->m_onIncomingBody = requestOptions.onIncomingBody;
                    toSeat->m_onIncomingHeaders = requestOptions.onIncomingHeaders;
                    toSeat->m_onIncomingHeadersBlockDone = requestOptions.onIncomingHeadersBlockDone;
                    toSeat->m_onStreamComplete = requestOptions.onStreamComplete;
                    toSeat->m_onStreamOutgoingBody = requestOptions.onStreamOutgoingBody;

                    wrapper->allocator = m_allocator;
                    options.user_data = wrapper;
                    toSeat->m_stream = aws_http_stream_new_client_request(&options);

                    if (!toSeat->m_stream)
                    {
                        wrapper->stream.reset();
                        wrapper->stream = nullptr;
                        aws_mem_release(m_allocator, reinterpret_cast<void *>(wrapper));
                        m_lastError = aws_last_error();
                        return nullptr;
                    }

                    return wrapper->stream;
                }

                m_lastError = aws_last_error();
                return nullptr;
            }

            bool HttpConnection::Close() noexcept
            {
                return aws_http_client_connection_close(m_connection) == AWS_OP_SUCCESS;
            }

            enum aws_http_outgoing_body_state HttpStream::s_onStreamOutgoingBody(
                struct aws_http_stream *,
                struct aws_byte_buf *buf,
                void *userData) noexcept
            {
                auto streamWrapper = reinterpret_cast<StreamWrapper *>(userData);

                if (streamWrapper->stream->m_onStreamOutgoingBody)
                {
                    return streamWrapper->stream->m_onStreamOutgoingBody(streamWrapper->stream, *buf);
                }

                return AWS_HTTP_OUTGOING_BODY_DONE;
            }

            void HttpStream::s_onIncomingHeaders(
                struct aws_http_stream *,
                const struct aws_http_header *headerArray,
                std::size_t numHeaders,
                void *userData) noexcept
            {
                auto streamWrapper = reinterpret_cast<StreamWrapper *>(userData);
                streamWrapper->stream->m_onIncomingHeaders(streamWrapper->stream, headerArray, numHeaders);
            }

            void HttpStream::s_onIncomingHeaderBlockDone(
                struct aws_http_stream *,
                bool hasBody,
                void *userData) noexcept
            {
                auto streamWrapper = reinterpret_cast<StreamWrapper *>(userData);

                if (streamWrapper->stream->m_onIncomingHeadersBlockDone)
                {
                    streamWrapper->stream->m_onIncomingHeadersBlockDone(streamWrapper->stream, hasBody);
                }
            }

            void HttpStream::s_onIncomingBody(
                struct aws_http_stream *,
                const struct aws_byte_cursor *data,
                size_t *outWindowUpdateSize,
                void *userData) noexcept
            {
                auto streamWrapper = reinterpret_cast<StreamWrapper *>(userData);

                if (streamWrapper->stream->m_onIncomingBody)
                {
                    streamWrapper->stream->m_onIncomingBody(streamWrapper->stream, *data, *outWindowUpdateSize);
                }
            }

            void HttpStream::s_onStreamComplete(struct aws_http_stream *, int errorCode, void *userData) noexcept
            {
                auto streamWrapper = reinterpret_cast<StreamWrapper *>(userData);
                streamWrapper->stream->m_onStreamComplete(streamWrapper->stream, errorCode);

                streamWrapper->stream.reset();
                streamWrapper->stream = nullptr;
                aws_mem_release(streamWrapper->allocator, reinterpret_cast<void *>(streamWrapper));
            }

            HttpStream::HttpStream(const std::shared_ptr<HttpConnection> &connection) noexcept
                : m_stream(nullptr), m_connection(connection)
            {
            }

            HttpStream::~HttpStream()
            {
                if (m_stream)
                {
                    aws_http_stream_release(m_stream);
                }

                if (m_connection)
                {
                    m_connection.reset();
                    m_connection = nullptr;
                }
            }

            const std::shared_ptr<HttpConnection> &HttpStream::GetConnection() const noexcept { return m_connection; }

            int HttpStream::GetIncommingResponseStatusCode() const noexcept
            {
                int status = 0;
                if (!aws_http_stream_get_incoming_response_status(m_stream, &status))
                {
                    return status;
                }

                return -1;
            }

            void HttpStream::UpdateWindow(std::size_t incrementSize) noexcept
            {
                aws_http_stream_update_window(m_stream, incrementSize);
            }
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
