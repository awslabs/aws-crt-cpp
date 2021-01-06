/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/http/HttpProxyStrategy.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/Bootstrap.h>
#include <iostream>

/*get kerberos usertoken*/
extern "C" char* get_kerberos_usertoken()
{
    /*TODO - this token will have to come from user code*/
    char *kerberos_testtoken = "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";
    return kerberos_testtoken;
}

/*print kerberos header*/
extern "C" void send_kerberos_header(size_t length,uint8_t *httpHeader,size_t length1,
                            uint8_t *httpHeader1,size_t num_headers)
{
    /*TODO - printing only for informational purpose - move to user area*/
    for (size_t i = 0; i < num_headers; ++i)
    {
        std::cout.write((char *)httpHeader, length);
        std::cout << ": ";
        std::cout.write((char *)httpHeader1, length1);
        std::cout << std::endl;
    }
}

/*print kerberos https status*/
extern "C" void send_kerberos_https_status(int httpStatusCode)
{
    /*TODO - printing only for informational purpose - move to user area*/
    std::cout << "httpStatusCode = " << httpStatusCode;
    
}

namespace Aws
{
    namespace Crt
    {
        namespace Http
        {
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
                        Aws::Crt::StlAllocator<UnmanagedConnection>(), connection, callbackData->allocator);

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

            bool HttpClientConnection::CreateConnection(
                const HttpClientConnectionOptions &connectionOptions,
                Allocator *allocator) noexcept
            {
                AWS_FATAL_ASSERT(connectionOptions.OnConnectionSetupCallback);
                AWS_FATAL_ASSERT(connectionOptions.OnConnectionShutdownCallback);

                auto *callbackData = New<ConnectionCallbackData>(allocator, allocator);

                if (!callbackData)
                {
                    return false;
                }
                callbackData->onConnectionShutdown = connectionOptions.OnConnectionShutdownCallback;
                callbackData->onConnectionSetup = connectionOptions.OnConnectionSetupCallback;

                aws_http_client_connection_options options;
                AWS_ZERO_STRUCT(options);
                options.self_size = sizeof(aws_http_client_connection_options);
                options.bootstrap = connectionOptions.Bootstrap->GetUnderlyingHandle();
                if (connectionOptions.TlsOptions)
                {
                    options.tls_options =
                        const_cast<aws_tls_connection_options *>(connectionOptions.TlsOptions->GetUnderlyingHandle());
                }
                options.allocator = allocator;
                options.user_data = callbackData;
                options.host_name = aws_byte_cursor_from_c_str(connectionOptions.HostName.c_str());
                options.port = connectionOptions.Port;
                options.initial_window_size = connectionOptions.InitialWindowSize;
                options.socket_options = &connectionOptions.SocketOptions.GetImpl();
                options.on_setup = HttpClientConnection::s_onClientConnectionSetup;
                options.on_shutdown = HttpClientConnection::s_onClientConnectionShutdown;
                options.manual_window_management = connectionOptions.ManualWindowManagement;

                if (aws_http_client_connect(&options))
                {
                    Delete(callbackData, allocator);
                    return false;
                }

                return true;
            }

            HttpClientConnection::HttpClientConnection(aws_http_connection *connection, Allocator *allocator) noexcept
                : m_connection(connection), m_allocator(allocator), m_lastError(AWS_ERROR_SUCCESS)
            {
            }

            std::shared_ptr<HttpClientStream> HttpClientConnection::NewClientStream(
                const HttpRequestOptions &requestOptions) noexcept
            {
                AWS_ASSERT(requestOptions.onIncomingHeaders);
                AWS_ASSERT(requestOptions.onStreamComplete);

                aws_http_make_request_options options;
                AWS_ZERO_STRUCT(options);
                options.self_size = sizeof(aws_http_make_request_options);
                options.request = requestOptions.request->GetUnderlyingMessage();
                options.on_response_body = HttpStream::s_onIncomingBody;
                options.on_response_headers = HttpStream::s_onIncomingHeaders;
                options.on_response_header_block_done = HttpStream::s_onIncomingHeaderBlockDone;
                options.on_complete = HttpStream::s_onStreamComplete;

                /* Do the same ref counting trick we did with HttpClientConnection. We need to maintain a reference
                 * internally (regardless of what the user does), until the Stream shuts down. */
                auto *toSeat = static_cast<HttpClientStream *>(aws_mem_acquire(m_allocator, sizeof(HttpClientStream)));

                if (toSeat)
                {
                    toSeat = new (toSeat) HttpClientStream(this->shared_from_this());

                    Allocator *captureAllocator = m_allocator;
                    std::shared_ptr<HttpClientStream> stream(
                        toSeat,
                        [captureAllocator](HttpStream *stream) { Delete(stream, captureAllocator); },
                        StlAllocator<HttpClientStream>(captureAllocator));

                    stream->m_onIncomingBody = requestOptions.onIncomingBody;
                    stream->m_onIncomingHeaders = requestOptions.onIncomingHeaders;
                    stream->m_onIncomingHeadersBlockDone = requestOptions.onIncomingHeadersBlockDone;
                    stream->m_onStreamComplete = requestOptions.onStreamComplete;
                    stream->m_callbackData.allocator = m_allocator;

                    // we purposefully do not set m_callbackData::stream because we don't want the reference count
                    // incremented until the request is kicked off via HttpClientStream::Activate(). Activate()
                    // increments the ref count.
                    options.user_data = &stream->m_callbackData;
                    stream->m_stream = aws_http_connection_make_request(m_connection, &options);

                    if (!stream->m_stream)
                    {
                        stream = nullptr;
                        m_lastError = aws_last_error();
                        return nullptr;
                    }

                    return stream;
                }

                m_lastError = aws_last_error();
                return nullptr;
            }

            bool HttpClientConnection::IsOpen() const noexcept { return aws_http_connection_is_open(m_connection); }

            void HttpClientConnection::Close() noexcept { aws_http_connection_close(m_connection); }

            HttpVersion HttpClientConnection::GetVersion() noexcept
            {
                return (HttpVersion)aws_http_connection_get_version(m_connection);
            }

            int HttpStream::s_onIncomingHeaders(
                struct aws_http_stream *,
                enum aws_http_header_block headerBlock,
                const struct aws_http_header *headerArray,
                size_t numHeaders,
                void *userData) noexcept
            {
                auto callbackData = static_cast<ClientStreamCallbackData *>(userData);
                callbackData->stream->m_onIncomingHeaders(*callbackData->stream, headerBlock, headerArray, numHeaders);

                return AWS_OP_SUCCESS;
            }

            int HttpStream::s_onIncomingHeaderBlockDone(
                struct aws_http_stream *,
                enum aws_http_header_block headerBlock,
                void *userData) noexcept
            {
                auto callbackData = static_cast<ClientStreamCallbackData *>(userData);

                if (callbackData->stream->m_onIncomingHeadersBlockDone)
                {
                    callbackData->stream->m_onIncomingHeadersBlockDone(*callbackData->stream, headerBlock);
                }

                return AWS_OP_SUCCESS;
            }

            int HttpStream::s_onIncomingBody(
                struct aws_http_stream *,
                const struct aws_byte_cursor *data,
                void *userData) noexcept
            {
                auto callbackData = static_cast<ClientStreamCallbackData *>(userData);

                if (callbackData->stream->m_onIncomingBody)
                {
                    callbackData->stream->m_onIncomingBody(*callbackData->stream, *data);
                }

                return AWS_OP_SUCCESS;
            }

            void HttpStream::s_onStreamComplete(struct aws_http_stream *, int errorCode, void *userData) noexcept
            {
                auto callbackData = static_cast<ClientStreamCallbackData *>(userData);
                callbackData->stream->m_onStreamComplete(*callbackData->stream, errorCode);
                callbackData->stream = nullptr;
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

            HttpClientStream::~HttpClientStream() {}

            int HttpClientStream::GetResponseStatusCode() const noexcept
            {
                int status = 0;
                if (!aws_http_stream_get_incoming_response_status(m_stream, &status))
                {
                    return status;
                }

                return -1;
            }

            bool HttpClientStream::Activate() noexcept
            {
                m_callbackData.stream = shared_from_this();
                if (aws_http_stream_activate(m_stream))
                {
                    m_callbackData.stream = nullptr;
                    return false;
                }

                return true;
            }

            void HttpStream::UpdateWindow(std::size_t incrementSize) noexcept
            {
                aws_http_stream_update_window(m_stream, incrementSize);
            }

            HttpClientConnectionProxyOptions::HttpClientConnectionProxyOptions()
                : HostName(), Port(0), TlsOptions(), ProxyStrategyFactory()
            {
            }

            HttpClientConnectionOptions::HttpClientConnectionOptions()
                : Bootstrap(nullptr), InitialWindowSize(SIZE_MAX), OnConnectionSetupCallback(),
                  OnConnectionShutdownCallback(), HostName(), Port(0), SocketOptions(), TlsOptions(), ProxyOptions(),
                  ManualWindowManagement(false)
            {
            }
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
