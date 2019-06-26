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
            HttpClientConnectionOptions::HttpClientConnectionOptions()
                : allocator(DefaultAllocator()), bootstrap(nullptr), initialWindowSize(SIZE_MAX), port(0),
                  socketOptions(nullptr), tlsConnOptions(nullptr)
            {
                AWS_ZERO_STRUCT(hostName);
            }

            /* This exists to handle aws_http_connection's shutdown callback, which might fire after
             * HttpClientConnection has been destroyed. */
            struct ConnectionCallbackData
            {
                explicit ConnectionCallbackData(Allocator *allocator) : allocator(allocator) {}
                std::weak_ptr<HttpClientConnection> connection;
                Allocator *allocator;
                OnConnectionSetup onConnectionSetup;
                OnConnectionShutdown onConnectionShutdown;
            };

            class UnmanagedConnection final : public HttpClientConnection
            {
              public:
                UnmanagedConnection(aws_http_connection *connection, Aws::Crt::Allocator *allocator)
                    : HttpClientConnection(connection, allocator)
                {
                }

                ~UnmanagedConnection() override
                {
                    if (m_connection)
                    {
                        aws_http_connection_release(m_connection);
                        m_connection = nullptr;
                    }
                }
            };

            void HttpClientConnection::s_onClientConnectionSetup(
                struct aws_http_connection *connection,
                int errorCode,
                void *user_data) noexcept
            {
                /**
                 * Allocate an HttpClientConnection and seat it to `ConnectionCallbackData`'s shared_ptr.
                 */
                auto *callbackData = static_cast<ConnectionCallbackData *>(user_data);
                if (!errorCode)
                {
                    auto connectionObj = std::allocate_shared<UnmanagedConnection>(
                        Aws::Crt::StlAllocator<UnmanagedConnection>(callbackData->allocator), connection, callbackData->allocator);

                    if (connectionObj)
                    {
                        callbackData->connection = connectionObj;
                        callbackData->onConnectionSetup(std::move(connectionObj), errorCode);
                        return;
                    }

                    aws_http_connection_release(connection);
                    errorCode = aws_last_error();
                }

                callbackData->onConnectionSetup(nullptr, errorCode);
                Delete(callbackData, callbackData->allocator);
            }

            void HttpClientConnection::s_onClientConnectionShutdown(
                struct aws_http_connection *connection,
                int errorCode,
                void *user_data) noexcept
            {
                (void)connection;
                auto *callbackData = static_cast<ConnectionCallbackData *>(user_data);

                /* Don't invoke callback if the connection object has expired. */
                if (auto connectionPtr = callbackData->connection.lock())
                {
                    callbackData->onConnectionShutdown(*connectionPtr, errorCode);
                }

                Delete(callbackData, callbackData->allocator);
            }

            bool HttpClientConnection::CreateConnection(const HttpClientConnectionOptions &connectionOptions) noexcept
            {
                AWS_ASSERT(connectionOptions.onConnectionSetup);
                AWS_ASSERT(connectionOptions.onConnectionShutdown);
                AWS_ASSERT(connectionOptions.socketOptions);

                auto *callbackData =
                    New<ConnectionCallbackData>(connectionOptions.allocator, connectionOptions.allocator);

                if (!callbackData)
                {
                    return false;
                }
                callbackData->onConnectionShutdown = connectionOptions.onConnectionShutdown;
                callbackData->onConnectionSetup = connectionOptions.onConnectionSetup;

                aws_http_client_connection_options options;
                AWS_ZERO_STRUCT(options);
                options.self_size = sizeof(aws_http_client_connection_options);
                options.bootstrap = connectionOptions.bootstrap->GetUnderlyingHandle();
                if (connectionOptions.tlsConnOptions)
                {
                    options.tls_options = const_cast<aws_tls_connection_options *>(
                        connectionOptions.tlsConnOptions->GetUnderlyingHandle());
                }
                options.allocator = connectionOptions.allocator;
                options.user_data = callbackData;
                options.host_name = connectionOptions.hostName;
                options.port = connectionOptions.port;
                options.initial_window_size = connectionOptions.initialWindowSize;
                options.socket_options = connectionOptions.socketOptions;
                options.on_setup = HttpClientConnection::s_onClientConnectionSetup;
                options.on_shutdown = HttpClientConnection::s_onClientConnectionShutdown;

                if (aws_http_client_connect(&options))
                {
                    Delete(callbackData, connectionOptions.allocator);
                    return false;
                }

                return true;
            }

            HttpClientConnection::HttpClientConnection(aws_http_connection *connection, Allocator *allocator) noexcept
                : m_connection(connection), m_allocator(allocator), m_lastError(AWS_ERROR_SUCCESS)
            {
            }

            struct ClientStreamCallbackData
            {
                Allocator *allocator;
                std::shared_ptr<HttpClientStream> stream;
            };

            std::shared_ptr<HttpClientStream> HttpClientConnection::NewClientStream(
                const HttpRequestOptions &requestOptions) noexcept
            {
                AWS_ASSERT(requestOptions.onIncomingHeaders);
                AWS_ASSERT(requestOptions.onStreamComplete);

                aws_http_request_options options;
                AWS_ZERO_STRUCT(options);
                options.self_size = sizeof(aws_http_request_options);
                options.uri = requestOptions.uri;
                options.method = requestOptions.method;
                options.header_array = requestOptions.headerArray;
                options.num_headers = requestOptions.headerArrayLength;
                options.stream_outgoing_body = HttpStream::s_onStreamOutgoingBody;
                options.on_response_body = HttpStream::s_onIncomingBody;
                options.on_response_headers = HttpStream::s_onIncomingHeaders;
                options.on_response_header_block_done = HttpStream::s_onIncomingHeaderBlockDone;
                options.on_complete = HttpStream::s_onStreamComplete;
                options.client_connection = m_connection;

                /* Do the same ref counting trick we did with HttpClientConnection. We need to maintain a reference
                 * internally (regardless of what the user does), until the Stream shuts down. */
                auto *toSeat = static_cast<HttpClientStream *>(aws_mem_acquire(m_allocator, sizeof(HttpStream)));

                if (toSeat)
                {
                    auto *callbackData = New<ClientStreamCallbackData>(m_allocator);
                    if (!callbackData)
                    {
                        aws_mem_release(m_allocator, toSeat);
                        return nullptr;
                    }

                    toSeat = new (toSeat) HttpClientStream(this->shared_from_this());

                    Allocator *captureAllocator = m_allocator;
                    callbackData->stream = std::shared_ptr<HttpClientStream>(
                        toSeat, [captureAllocator](HttpStream *stream) { Delete(stream, captureAllocator); });

                    toSeat->m_onIncomingBody = requestOptions.onIncomingBody;
                    toSeat->m_onIncomingHeaders = requestOptions.onIncomingHeaders;
                    toSeat->m_onIncomingHeadersBlockDone = requestOptions.onIncomingHeadersBlockDone;
                    toSeat->m_onStreamComplete = requestOptions.onStreamComplete;
                    toSeat->m_onStreamOutgoingBody = requestOptions.onStreamOutgoingBody;

                    callbackData->allocator = m_allocator;
                    options.user_data = callbackData;
                    toSeat->m_stream = aws_http_stream_new_client_request(&options);

                    if (!toSeat->m_stream)
                    {
                        callbackData->stream = nullptr;
                        Delete(callbackData, m_allocator);
                        m_lastError = aws_last_error();
                        return nullptr;
                    }

                    return callbackData->stream;
                }

                m_lastError = aws_last_error();
                return nullptr;
            }

            bool HttpClientConnection::IsOpen() const noexcept { return aws_http_connection_is_open(m_connection); }

            void HttpClientConnection::Close() noexcept { aws_http_connection_close(m_connection); }

            enum aws_http_outgoing_body_state HttpStream::s_onStreamOutgoingBody(
                struct aws_http_stream *,
                struct aws_byte_buf *buf,
                void *userData) noexcept
            {
                auto callbackData = static_cast<ClientStreamCallbackData *>(userData);

                if (callbackData->stream->m_onStreamOutgoingBody)
                {
                    return callbackData->stream->m_onStreamOutgoingBody(*callbackData->stream, *buf);
                }

                return AWS_HTTP_OUTGOING_BODY_DONE;
            }

            void HttpStream::s_onIncomingHeaders(
                struct aws_http_stream *,
                const struct aws_http_header *headerArray,
                std::size_t numHeaders,
                void *userData) noexcept
            {
                auto callbackData = static_cast<ClientStreamCallbackData *>(userData);
                callbackData->stream->m_onIncomingHeaders(*callbackData->stream, headerArray, numHeaders);
            }

            void HttpStream::s_onIncomingHeaderBlockDone(
                struct aws_http_stream *,
                bool hasBody,
                void *userData) noexcept
            {
                auto callbackData = static_cast<ClientStreamCallbackData *>(userData);

                if (callbackData->stream->m_onIncomingHeadersBlockDone)
                {
                    callbackData->stream->m_onIncomingHeadersBlockDone(*callbackData->stream, hasBody);
                }
            }

            void HttpStream::s_onIncomingBody(
                struct aws_http_stream *,
                const struct aws_byte_cursor *data,
                size_t *outWindowUpdateSize,
                void *userData) noexcept
            {
                auto callbackData = static_cast<ClientStreamCallbackData *>(userData);

                if (callbackData->stream->m_onIncomingBody)
                {
                    callbackData->stream->m_onIncomingBody(*callbackData->stream, *data, *outWindowUpdateSize);
                }
            }

            void HttpStream::s_onStreamComplete(struct aws_http_stream *, int errorCode, void *userData) noexcept
            {
                auto callbackData = static_cast<ClientStreamCallbackData *>(userData);
                callbackData->stream->m_onStreamComplete(*callbackData->stream, errorCode);

                callbackData->stream = nullptr;
                Delete(callbackData, callbackData->allocator);
            }

            HttpStream::HttpStream(const std::shared_ptr<HttpClientConnection> &connection) noexcept
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
                    m_connection = nullptr;
                }
            }

            HttpClientConnection &HttpStream::GetConnection() const noexcept { return *m_connection; }

            HttpClientStream::HttpClientStream(const std::shared_ptr<HttpClientConnection> &connection) noexcept
                : HttpStream(connection)
            {
            }

            int HttpClientStream::GetResponseStatusCode() const noexcept
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
