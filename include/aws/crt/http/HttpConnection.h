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
#include <aws/http/connection.h>
#include <aws/http/request_response.h>

#include <aws/crt/Types.h>
#include <aws/crt/io/Bootstrap.h>
#include <aws/crt/io/TlsOptions.h>

#include <functional>
#include <memory>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            class ClientBootstrap;
        }

        namespace Http
        {
            class HttpConnection;
            class HttpStream;

            using OnConnectionSetup = std::function<void(const std::shared_ptr<HttpConnection> &, int errorCode)>;
            using OnConnectionShutdown = std::function<void(const std::shared_ptr<HttpConnection> &, int errorCode)>;

            using OnStreamOutgoingBody = std::function<
                enum aws_http_outgoing_body_state(const std::shared_ptr<HttpStream> &stream, ByteBuf &buffer)>;
            using OnIncomingHeaders = std::function<void(
                const std::shared_ptr<HttpStream> &stream,
                const struct aws_http_header *headersArray,
                std::size_t headersCount)>;
            using OnIncomingHeadersBlockDone =
                std::function<void(const std::shared_ptr<HttpStream> &stream, bool hasBody)>;
            using OnIncomingBody = std::function<void(
                const std::shared_ptr<HttpStream> &stream,
                const ByteCursor &data,
                std::size_t &outWindowUpdateSize)>;
            using OnStreamComplete = std::function<void(const std::shared_ptr<HttpStream> &stream, int errorCode)>;

            struct HttpRequestOptions
            {
                ByteCursor method;
                ByteCursor uri;
                aws_http_header *headerArray;
                std::size_t headerArrayLength;
                OnStreamOutgoingBody onStreamOutgoingBody;
                OnIncomingHeaders onIncomingHeaders;
                OnIncomingHeadersBlockDone onIncomingHeadersBlockDone;
                OnIncomingBody onIncomingBody;
                OnStreamComplete onStreamComplete;
            };

            class AWS_CRT_CPP_API HttpStream final
            {
              public:
                ~HttpStream();
                HttpStream(const HttpStream &) = delete;
                HttpStream(HttpStream &&) = delete;
                HttpStream &operator=(const HttpStream &) = delete;
                HttpStream &operator=(HttpStream &&) = delete;

                const std::shared_ptr<HttpConnection> &GetConnection() const noexcept;
                int GetIncommingResponseStatusCode() const noexcept;
                void UpdateWindow(std::size_t incrementSize) noexcept;

              private:
                HttpStream(const std::shared_ptr<HttpConnection> &connection) noexcept;

                aws_http_stream *m_stream;
                std::shared_ptr<HttpConnection> m_connection;
                OnStreamOutgoingBody m_onStreamOutgoingBody;
                OnIncomingHeaders m_onIncomingHeaders;
                OnIncomingHeadersBlockDone m_onIncomingHeadersBlockDone;
                OnIncomingBody m_onIncomingBody;
                OnStreamComplete m_onStreamComplete;

                static enum aws_http_outgoing_body_state s_onStreamOutgoingBody(
                    struct aws_http_stream *stream,
                    struct aws_byte_buf *buf,
                    void *user_data) noexcept;
                static void s_onIncomingHeaders(
                    struct aws_http_stream *stream,
                    const struct aws_http_header *header_array,
                    size_t num_headers,
                    void *user_data) noexcept;
                static void s_onIncomingHeaderBlockDone(
                    struct aws_http_stream *stream,
                    bool has_body,
                    void *user_data) noexcept;
                static void s_onIncomingBody(
                    struct aws_http_stream *stream,
                    const struct aws_byte_cursor *data,
                    size_t *out_window_update_size,
                    void *user_data) noexcept;
                static void s_onStreamComplete(
                    struct aws_http_stream *stream,
                    int error_code,
                    void *user_data) noexcept;

                friend class HttpConnection;
            };

            class AWS_CRT_CPP_API HttpConnection final : public std::enable_shared_from_this<HttpConnection>
            {
              public:
                ~HttpConnection();
                HttpConnection(const HttpConnection &) = delete;
                HttpConnection(HttpConnection &&) = delete;
                HttpConnection &operator=(const HttpConnection &) = delete;
                HttpConnection &operator=(HttpConnection &&) = delete;

                std::shared_ptr<HttpStream> NewStream(const HttpRequestOptions &requestOptions) noexcept;
                bool Close() noexcept;

              private:
                HttpConnection(aws_http_connection *m_connection, Allocator *allocator) noexcept;
                aws_http_connection *m_connection;
                Allocator *m_allocator;
                bool m_good;
                int m_lastError;

                friend class HttpClient;
            };

            class AWS_CRT_CPP_API HttpClient final
            {
              public:
                HttpClient(
                    Io::ClientBootstrap *bootstrap,
                    std::size_t initialWindowSize = SIZE_MAX,
                    Allocator *allocator = DefaultAllocator()) noexcept;

                bool NewConnection(
                    const ByteCursor &hostName,
                    uint16_t port,
                    const Io::SocketOptions &socketOptions,
                    const Io::TlsConnectionOptions &tlsConnOptions) const noexcept;
                bool NewConnection(const ByteCursor &hostName, uint16_t port, const Io::SocketOptions &socketOptions)
                    const noexcept;

                OnConnectionSetup onConnectionSetup;
                OnConnectionShutdown onConnectionShutdown;

              private:
                Allocator *m_allocator;
                Io::ClientBootstrap *m_bootstrap;
                size_t m_initialWindowSize;

                static void s_onClientConnectionSetup(
                    struct aws_http_connection *connection,
                    int error_code,
                    void *user_data) noexcept;
                static void s_onClientConnectionShutdown(
                    struct aws_http_connection *connection,
                    int error_code,
                    void *user_data) noexcept;
            };
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
