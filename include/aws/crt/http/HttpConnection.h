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
            class HttpRequest;
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
             * On HttpStream, this function can be empty if you are not expecting a body (e.g. a HEAD request).
             */
            using OnIncomingBody = std::function<void(HttpStream &stream, const ByteCursor &data)>;

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

                HttpRequest *request;
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
                OnIncomingHeaders m_onIncomingHeaders;
                OnIncomingHeadersBlockDone m_onIncomingHeadersBlockDone;
                OnIncomingBody m_onIncomingBody;
                OnStreamComplete m_onStreamComplete;

                static int s_onIncomingHeaders(
                    struct aws_http_stream *stream,
                    const struct aws_http_header *header_array,
                    size_t num_headers,
                    void *user_data) noexcept;
                static int s_onIncomingHeaderBlockDone(
                    struct aws_http_stream *stream,
                    bool has_body,
                    void *user_data) noexcept;
                static int s_onIncomingBody(
                    struct aws_http_stream *stream,
                    const struct aws_byte_cursor *data,
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

            enum class AwsHttpProxyAuthenticationType {
              None = AWS_HPAT_NONE,
              Basic,
            };

            class HttpClientConnectionProxyOptions
            {
            public:
              HttpClientConnectionProxyOptions(struct aws_allocator *allocator = DefaultAllocator());
              HttpClientConnectionProxyOptions(const HttpClientConnectionProxyOptions &rhs);
              HttpClientConnectionProxyOptions(HttpClientConnectionProxyOptions &&rhs);

              HttpClientConnectionProxyOptions &operator =(const HttpClientConnectionProxyOptions &rhs);
              HttpClientConnectionProxyOptions &operator =(HttpClientConnectionProxyOptions &&rhs);

              ~HttpClientConnectionProxyOptions();

              void SetHostName(const String &hostName) { m_hostName = hostName; }
              const String &GetHostName() const { return m_hostName; }

              void SetPort(uint16_t port) noexcept { m_port = port; }
              uint16_t GetPort() const noexcept { return m_port; }

              void SetTlsOptions(const Io::TlsConnectionOptions &options) noexcept;
              Io::TlsConnectionOptions *GetTlsOptions() const noexcept { return m_tlsOptions.get(); }

              void SetAuthenticationType(enum AwsHttpProxyAuthenticationType authType) { m_authType = authType; }
              enum AwsHttpProxyAuthenticationType GetAuthenticationType() const { return m_authType; }

              void SetBasicAuthUsername(const String &username) { m_basicAuthUsername = username; }
              const String &GetBasicAuthUsername() const { return m_basicAuthUsername; }

              void SetBasicAuthPassword(const String &password) { m_basicAuthPassword = password; }
              const String &GetBasicAuthPassword() const { return m_basicAuthPassword; }

              explicit operator bool() const { return m_initializationErrorCode == AWS_ERROR_SUCCESS; }
              int GetInitializationErrorCode() const { return m_initializationErrorCode; }

            private:

              struct aws_allocator *m_allocator;
              int m_initializationErrorCode;
              
              String m_hostName;
              uint16_t m_port;
              ScopedResource<Io::TlsConnectionOptions> m_tlsOptions;
              AwsHttpProxyAuthenticationType m_authType;
              String m_basicAuthUsername;
              String m_basicAuthPassword;
            };

            class HttpClientConnectionOptions
            {
            public:
                HttpClientConnectionOptions(struct aws_allocator *allocator = DefaultAllocator()) noexcept;
              HttpClientConnectionOptions(const HttpClientConnectionOptions &rhs);
              HttpClientConnectionOptions(HttpClientConnectionOptions &&rhs) noexcept;

              ~HttpClientConnectionOptions();

              HttpClientConnectionOptions &operator =(const HttpClientConnectionOptions &rhs);
              HttpClientConnectionOptions &operator =(const HttpClientConnectionOptions &&rhs) noexcept;

              void SetBootstrap(Io::ClientBootstrap *bootstrap) noexcept { m_bootstrap = bootstrap; }
              Io::ClientBootstrap *GetBootstrap() const noexcept { return m_bootstrap; }

              void SetInitialWindowSize(size_t initialWindowSize) noexcept { m_initialWindowSize = initialWindowSize; }
              size_t GetInitialWindowSize() const noexcept { return m_initialWindowSize; }

              void SetOnConnectionSetupCallback(const OnConnectionSetup &callback) { m_onConnectionSetup = callback; }
              const OnConnectionSetup &GetOnConnectionSetupCallback() const { return m_onConnectionSetup; }

              void SetOnConnectionShutdownCallback(const OnConnectionShutdown &callback) { m_onConnectionShutdown = callback; }
              const OnConnectionShutdown &GetOnConnectionShutdownCallback() const { return m_onConnectionShutdown; }

              void SetHostName(const String &hostName) { m_hostName = hostName; }
              const String &GetHostName() const { return m_hostName; }

              void SetPort(uint16_t port) noexcept { m_port = port; }
              uint16_t GetPort() const noexcept { return m_port; }

              void SetSocketOptions(const Io::SocketOptions &options) noexcept { m_socketOptions = options; }
              const Io::SocketOptions &GetSocketOptions() const noexcept { return m_socketOptions; }

              void SetTlsOptions(const Io::TlsConnectionOptions &options) noexcept;
              Io::TlsConnectionOptions *GetTlsOptions() const noexcept { return m_tlsOptions.get(); }

              void SetProxyOptions(const HttpClientConnectionProxyOptions &options) noexcept;
              HttpClientConnectionProxyOptions *GetProxyOptions() const noexcept { return m_proxyOptions.get(); }

              explicit operator bool() const { return m_initializationErrorCode == AWS_ERROR_SUCCESS; }
              int GetInitializationErrorCode() const { return m_initializationErrorCode; }

            private:

                struct aws_allocator *m_allocator;

                int m_initializationErrorCode;

                /**
                 * Client bootstrap to use for setting up and tearing down connections.
                 */
                Io::ClientBootstrap *m_bootstrap;

                /**
                 *  `initialWindowSize` will set the TCP read window
                 * allowed for Http 1.1 connections and Initial Windows for H2 connections.
                 */
                size_t m_initialWindowSize;

                /**
                 * See `OnConnectionSetup` for more info. This value cannot be empty.
                 */
                OnConnectionSetup m_onConnectionSetup;

                /**
                 * See `OnConnectionShutdown` for more info. This value cannot be empty.
                 */
                OnConnectionShutdown m_onConnectionShutdown;

                /**
                 * hostname to connect to.
                 */
                String m_hostName;

                /**
                 * port on host to connect to.
                 */
                uint16_t m_port;

                /**
                 * socket options to use for connection.
                 */
                Io::SocketOptions m_socketOptions;

                /**
                 * Tls options to use. If null, and http (plain-text) connection will be attempted. Otherwise,
                 * https will be used.
                 */
                ScopedResource<Io::TlsConnectionOptions> m_tlsOptions;

                /*
                 * Http proxy options to use.  If null, proxy connection logic will not be used.
                 */
                ScopedResource<HttpClientConnectionProxyOptions> m_proxyOptions;
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
                static bool CreateConnection(const HttpClientConnectionOptions &connectionOptions, Allocator *allocator) noexcept;

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
