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
            class HttpClientConnection;
            class HttpStream;
            class HttpClientStream;
            using HttpHeader = aws_http_header;

            /**
             * Invoked upon connection setup, whether it was successful or not. If the connection was
             * successfully established, `connection` will be valid and errorCode will be AWS_ERROR_SUCCESS.
             * Upon an error, `connection` will not be valid, and errorCode will contain the cause of the connection
             * failure.
             */
            using OnConnectionSetup =
                std::function<void(const std::shared_ptr<HttpClientConnection> &connection, int errorCode)>;

            /**
             * Invoked upon connection shutdown. `connection` will always be a valid pointer. `errorCode` will specify
             * shutdown reason. A graceful connection close will set `errorCode` to AWS_ERROR_SUCCESS.
             * Internally, the connection pointer will be unreferenced immediately after this call; if you took a
             * reference to it in OnConnectionSetup(), you'll need to release your reference before the underlying
             * memory is released. If you never took a reference to it, the resources for the connection will be
             * immediately released after completion of this callback.
             */
            using OnConnectionShutdown = std::function<void(HttpClientConnection &connection, int errorCode)>;

            /**
             * Called as part of the outgoing http message's body (request in client mode, and response in server mode).
             * `buffer` contains the buffer for you to write into. Keep in mind, that `buffer` may already have
             * pending data in it, so always append after buffer.buf + buffer.len. You can write up to buffer.capacity
             * - buffer.len into the buffer.
             *
             * You need not write anything to the buffer to keep the stream alive. As long as you return:
             * AWS_HTTP_OUTGOING_BODY_IN_PROGRESS, you will continue to receive this callback until you
             * return AWS_HTTP_OUTGOING_BODY_DONE.
             *
             * This parameter can be empty on the HttpStream object if you will not be sending a body.
             */
            using OnStreamOutgoingBody =
                std::function<enum aws_http_outgoing_body_state(HttpStream &stream, ByteBuf &buffer)>;

            /**
             * Called as headers are received from the peer. `headersArray` will contain the header value
             * read from the wire. The number of entries in `headersArray` are specified in `headersCount`.
             *
             * Keep in mind that this function will likely be called multiple times until all headers are received.
             *
             * On HttpStream, this function must be set.
             */
            using OnIncomingHeaders =
                std::function<void(HttpStream &stream, const HttpHeader *headersArray, std::size_t headersCount)>;

            /**
             * Invoked when the headers portion of the message has been completely received. `hasBody` will indicate
             * if there is an incoming body.
             *
             * On HttpStream, this function can be empty.
             */
            using OnIncomingHeadersBlockDone = std::function<void(HttpStream &stream, bool hasBody)>;

            /**
             * Invoked as chunks of the body are read. `data` contains the data read from the wire. If chunked encoding
             * was used, it will already be decoded (TBD).
             *
             * `outWindowUpdateSize` is how much to increment the window once this data is processed.
             * By default, it is the size of the data which has just come in.
             * Leaving this value untouched will increment the window back to its original size.
             * Setting this value to 0 will prevent the update and let the window shrink.
             * The window can be manually updated via Aws::Crt::Http::HttpStream::UpdateWindow()
             *
             * On HttpStream, this function can be empty if you are not expecting a body (e.g. a HEAD request).
             */
            using OnIncomingBody =
                std::function<void(HttpStream &stream, const ByteCursor &data, std::size_t &outWindowUpdateSize)>;

            /**
             * Invoked upon completion of the stream. This means the request has been sent and a completed response
             * has been received (in client mode), or the request has been received and the response has been completed.
             *
             * In H2, this will mean RST_STREAM state has been reached for the stream.
             *
             * On HttpStream, this function must be set.
             */
            using OnStreamComplete = std::function<void(HttpStream &stream, int errorCode)>;

            /**
             * POD structure used for setting up an Http Request
             */
            struct HttpRequestOptions
            {
                /**
                 * Http verb to use (e.g. GET, POST, PUT, DELETE, HEAD....). If you are using a custom verb, that
                 * can be set here as well.
                 *
                 * this value is copied internally.
                 */
                ByteCursor method;
                /**
                 * Usually the Path and Query portion of the request uri (assuming the host header has been set).
                 * There's not validation on this, it can also be the absolute uri if you want/need that.
                 *
                 * A helpful utility for setting this value can be found in Aws::Crt::Io::Uri
                 *
                 * this value is copied internally.
                 */
                ByteCursor uri;
                /**
                 * Array of headers to use for the Request. These values will be copied internally.
                 */
                HttpHeader *headerArray;
                /**
                 * Length of `headerArray`.
                 */
                std::size_t headerArrayLength;
                /**
                 * See `OnConnectionShutdown` for more info. This value can be empty if you don't need to send a body.
                 */
                OnStreamOutgoingBody onStreamOutgoingBody;
                /**
                 * See `OnIncomingHeaders` for more info. This value must be set.
                 */
                OnIncomingHeaders onIncomingHeaders;
                OnIncomingHeadersBlockDone onIncomingHeadersBlockDone;
                /**
                 * See `OnIncomingBody` for more info. This value can be empty if you will not be receiving a body.
                 */
                OnIncomingBody onIncomingBody;
                /**
                 * See `OnStreamComplete` for more info. This value can be empty.
                 */
                OnStreamComplete onStreamComplete;
            };

            /**
             * Represents a single http message exchange (request/response) or in H2, it can also represent
             * a PUSH_PROMISE followed by the accompanying Response.
             */
            class AWS_CRT_CPP_API HttpStream
            {
              public:
                virtual ~HttpStream();
                HttpStream(const HttpStream &) = delete;
                HttpStream(HttpStream &&) = delete;
                HttpStream &operator=(const HttpStream &) = delete;
                HttpStream &operator=(HttpStream &&) = delete;

                /**
                 * Get the underlying connection for the stream.
                 */
                HttpClientConnection &GetConnection() const noexcept;

                virtual int GetResponseStatusCode() const noexcept = 0;

                /**
                 * Updates the read window on the connection. In Http 1.1 this relieves TCP back pressure, in H2
                 * this will trigger two WINDOW_UPDATE frames, one for the connection and one for the stream.
                 *
                 * You do not need to call this unless you utilized the `outWindowUpdateSize` in `OnIncomingBody`.
                 * See `OnIncomingBody` for more information.
                 *
                 * `incrementSize` is the amount to update the read window by.
                 */
                void UpdateWindow(std::size_t incrementSize) noexcept;

              protected:
                aws_http_stream *m_stream;
                std::shared_ptr<HttpClientConnection> m_connection;
                HttpStream(const std::shared_ptr<HttpClientConnection> &connection) noexcept;

              private:
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

                friend class HttpClientConnection;
            };

            class HttpClientStream final : public HttpStream
            {
              public:
                ~HttpClientStream() = default;
                HttpClientStream(const HttpClientStream &) = delete;
                HttpClientStream(HttpClientStream &&) = delete;
                HttpClientStream &operator=(const HttpClientStream &) = delete;
                HttpClientStream &operator=(HttpClientStream &&) = delete;

                /**
                 * If this stream was initiated as a request, assuming the headers of the response has been
                 * received, this value contains the Http Response Code.                 *
                 */
                virtual int GetResponseStatusCode() const noexcept override;

              private:
                HttpClientStream(const std::shared_ptr<HttpClientConnection> &connection) noexcept;

                friend class HttpClientConnection;
            };

            struct HttpClientConnectionOptions
            {
                HttpClientConnectionOptions();
                Allocator *allocator;

                /**
                 * Client bootstrap to use for setting up and tearing down connections.
                 */
                Io::ClientBootstrap *bootstrap;
                /**
                 *  `initialWindowSize` will set the TCP read window
                 * allowed for Http 1.1 connections and Initial Windows for H2 connections.
                 */
                size_t initialWindowSize;
                /**
                 * See `OnConnectionSetup` for more info. This value cannot be empty.
                 */
                OnConnectionSetup onConnectionSetup;
                /**
                 * See `OnConnectionShutdown` for more info. This value cannot be empty.
                 */
                OnConnectionShutdown onConnectionShutdown;

                /**
                 * hostname to connect to.
                 */
                ByteCursor hostName;

                /**
                 * port on host to connect to.
                 */
                uint16_t port;

                /**
                 * socket options to use for connection.
                 */
                Io::SocketOptions *socketOptions;

                /**
                 * Tls options to use. If null, and http (plain-text) connection will be attempted. Otherwise,
                 * https will be used.
                 */
                Io::TlsConnectionOptions *tlsConnOptions;
            };

            /**
             * Represents a connection from a Http Client to a Server.
             */
            class AWS_CRT_CPP_API HttpClientConnection : public std::enable_shared_from_this<HttpClientConnection>
            {
              public:
                virtual ~HttpClientConnection() = default;
                HttpClientConnection(const HttpClientConnection &) = delete;
                HttpClientConnection(HttpClientConnection &&) = delete;
                HttpClientConnection &operator=(const HttpClientConnection &) = delete;
                HttpClientConnection &operator=(HttpClientConnection &&) = delete;

                /**
                 * Make a new client initiated request on this connection.
                 *
                 * If you take a reference to the return value, the memory and resources for the connection
                 * and stream will not be cleaned up until you release it. You can however, release the reference
                 * as soon as you don't need it anymore. The internal reference count ensures the resources will
                 * not be freed until the stream is completed.
                 *
                 * Returns an instance of HttpStream upon success and nullptr on failure.
                 */
                std::shared_ptr<HttpClientStream> NewClientStream(const HttpRequestOptions &requestOptions) noexcept;

                /**
                 * Returns true unless the connection is closed or closing.
                 */
                bool IsOpen() const noexcept;

                /**
                 * Initiate a shutdown of the connection. Sometimes, connections are persistent and you want
                 * to close them before shutting down your application or whatever is consuming this interface.
                 *
                 * Assuming `OnConnectionShutdown` has not already been invoked, it will be invoked as a result of this
                 * call.
                 */
                void Close() noexcept;

                int LastError() const noexcept { return m_lastError; }

                /**
                 * Create a new Https Connection to hostName:port, using `socketOptions` for tcp options and
                 * `tlsConnOptions` for TLS/SSL options. If `tlsConnOptions` is null http (plain-text) will be used.
                 *
                 * returns true on success, and false on failure. If false is returned, `onConnectionSetup` will not
                 * be invoked. On success, `onConnectionSetup` will be called, either with a connection, or an
                 * errorCode.
                 */
                static bool CreateConnection(const HttpClientConnectionOptions &connectionOptions) noexcept;

              protected:
                HttpClientConnection(aws_http_connection *m_connection, Allocator *allocator) noexcept;
                aws_http_connection *m_connection;

              private:
                Allocator *m_allocator;
                int m_lastError;

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
