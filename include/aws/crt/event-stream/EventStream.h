#pragma once
/* Copyright 2010-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

* This file is generated
*/
#include <aws/crt/Exports.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/Types.h>
#include <aws/crt/DateTime.h>
#include <aws/crt/UUID.h>
#include <aws/crt/io/SocketOptions.h>
#include <aws/crt/io/TlsOptions.h>
#include <aws/crt/io/EventLoopGroup.h>

#include <aws/event-stream/event_stream_rpc_client.h>

#include <atomic>
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

        namespace Eventstream
        {
            class EventStreamHeader;
            class EventstreamRpcClient;
            class EventstreamRpcConnection;
            class MessageAmendment;

            using HeaderType = aws_event_stream_header_value_type;
            using MessageType = aws_event_stream_rpc_message_type;

            using OnMessageFlush = std::function<void(int errorCode)>;

            /**
             * Invoked upon connection setup, whether it was successful or not. If the connection was
             * successfully established, `connection` will be valid and errorCode will be AWS_ERROR_SUCCESS.
             * Upon an error, `connection` will not be valid, and errorCode will contain the cause of the connection
             * failure.
             */
            using OnConnect = std::function<void(const std::shared_ptr<EventstreamRpcConnection> &connection)>;

            /**
             * Invoked upon connection shutdown. `connection` will always be a valid pointer. `errorCode` will specify
             * shutdown reason. A graceful connection close will set `errorCode` to AWS_ERROR_SUCCESS.
             * Internally, the connection pointer will be unreferenced immediately after this call; if you took a
             * reference to it in OnConnectionSetup(), you'll need to release your reference before the underlying
             * memory is released. If you never took a reference to it, the resources for the connection will be
             * immediately released after completion of this callback.
             */
            using OnDisconnect = std::function<void(const std::shared_ptr<Eventstream::EventstreamRpcConnection> &newConnection, int errorCode)>;

            using OnError = std::function<bool(int errorCode)>;

            using OnPing = std::function<void(Crt::List<EventStreamHeader> headers, ByteBuf payload)>;

            using ConnectMessageAmender = std::function<MessageAmendment(void)>;

            class AWS_CRT_CPP_API EventStreamHeader final
            {
                public:
                    EventStreamHeader(const EventStreamHeader &rhs) = default;
                    EventStreamHeader(EventStreamHeader &&rhs) = default;
                    EventStreamHeader(const struct aws_event_stream_header_value_pair& header);
                    EventStreamHeader(const String& name, bool value);
                    EventStreamHeader(const String& name, int8_t value);
                    EventStreamHeader(const String& name, int16_t value);
                    EventStreamHeader(const String& name, int32_t value);
                    EventStreamHeader(const String& name, int64_t value);
                    EventStreamHeader(const String& name, DateTime& value);
                    EventStreamHeader(const String& name, const String& value);
                    EventStreamHeader(const String& name, ByteBuf& value);
                    EventStreamHeader(const String& name, Crt::UUID value);

                    HeaderType GetHeaderType();
                    String& GetHeaderName();
                    void SetHeaderName(String&);

                    bool GetValueAsBoolean(bool&);
                    bool GetValueAsByte(int8_t&);
                    bool GetValueAsShort(int16_t&);
                    bool GetValueAsInt(int32_t&);
                    bool GetValueAsLong(int64_t&);
                    bool GetValueAsTimestamp(DateTime&);
                    bool GetValueAsBytes(ByteBuf&);
                    bool GetValueAsUUID(Crt::UUID&);

                    void SetValue(bool value);
                    void SetValue(int8_t value);
                    void SetValue(int16_t value);
                    void SetValue(int32_t value);
                    void SetValue(int64_t value);
                    void SetValue(DateTime& value);
                    void SetValue(ByteBuf value);
                    void SetValue(Crt::UUID value);

                    struct aws_event_stream_header_value_pair * GetUnderlyingHandle();

                    bool operator==(const EventStreamHeader &other) const noexcept;
                private:
                    struct aws_event_stream_header_value_pair m_underlyingHandle;
                    ByteBuf valueByteBuf;
            };

            class AWS_CRT_CPP_API MessageAmendment final
            {
                public:
                    MessageAmendment() = default;
                    MessageAmendment(const MessageAmendment &rhs) = default;
                    MessageAmendment(MessageAmendment &&rhs) = default;
                    MessageAmendment(const Crt::Optional<Crt::List<EventStreamHeader> >& headers, const Crt::Optional<ByteBuf>& payload) noexcept;
                    MessageAmendment(const Crt::List<EventStreamHeader>& headers) noexcept;
                    MessageAmendment(const ByteBuf& payload) noexcept;
                    Crt::Optional<Crt::List<EventStreamHeader> > &GetHeaders() noexcept;
                    Crt::Optional<ByteBuf> &GetPayload() noexcept;
                private:
                    Crt::Optional<Crt::List<EventStreamHeader> > m_headers;
                    Crt::Optional<ByteBuf> m_payload;
            };

            /**
             * Configuration structure holding all options relating to eventstream RPC connection establishment
             */
            class AWS_CRT_CPP_API EventstreamRpcConnectionOptions final
            {
                public:
                    EventstreamRpcConnectionOptions();
                    EventstreamRpcConnectionOptions(const EventstreamRpcConnectionOptions &rhs) = default;
                    EventstreamRpcConnectionOptions(EventstreamRpcConnectionOptions &&rhs) = default;

                    ~EventstreamRpcConnectionOptions() = default;

                    EventstreamRpcConnectionOptions &operator=(const EventstreamRpcConnectionOptions &rhs) = default;
                    EventstreamRpcConnectionOptions &operator=(EventstreamRpcConnectionOptions &&rhs) = default;

                    Io::ClientBootstrap *Bootstrap;
                    Io::SocketOptions SocketOptions;
                    Crt::Optional<Io::TlsConnectionOptions> TlsOptions;
                    Crt::String HostName;
                    uint16_t Port;
                    OnConnect OnConnectCallback;
                    OnDisconnect OnDisconnectCallback;
                    OnError OnErrorCallback;
                    OnPing OnPingCallback;
                    ConnectMessageAmender ConnectMessageAmenderCallback;
            };

            class AWS_CRT_CPP_API EventstreamRpcConnection : public std::enable_shared_from_this<EventstreamRpcConnection>
            {
                public:
                    virtual ~EventstreamRpcConnection() = default;
                    EventstreamRpcConnection(const EventstreamRpcConnection &) = delete;
                    EventstreamRpcConnection(EventstreamRpcConnection &&) = delete;
                    EventstreamRpcConnection &operator=(const EventstreamRpcConnection &) = delete;
                    EventstreamRpcConnection &operator=(EventstreamRpcConnection &&) = delete;

                    static bool CreateConnection(
                        const EventstreamRpcConnectionOptions& config,
                        Allocator *allocator
                    ) noexcept;

                    void SendPing(
                        Crt::Optional<Crt::List<EventStreamHeader> >& headers,
                        Crt::Optional<ByteBuf>& payload,
                        OnMessageFlush onMessageFlushCallback
                    ) noexcept;

                    void SendPingResponse(
                        Crt::Optional<Crt::List<EventStreamHeader> >& headers,
                        Crt::Optional<ByteBuf>& payload,
                        OnMessageFlush onMessageFlushCallback
                    ) noexcept;

                    void Close() noexcept;
                    void Close(int errorCode) noexcept;

                    /**
                     * @return true if the instance is in a valid state, false otherwise.
                    */
                    operator bool() const noexcept;
                    /**
                     * @return the value of the last aws error encountered by operations on this instance.
                    */
                    int LastError() const noexcept;
                protected:
                    EventstreamRpcConnection(
                        struct aws_event_stream_rpc_client_connection *connection,
                        Allocator *allocator
                    ) noexcept;
                    struct aws_event_stream_rpc_client_connection *m_underlyingConnection;
                private:
                    enum ClientState {
                        DISCONNECTED = 1,
                        CONNECTING_TO_SOCKET,
                        WAITING_FOR_CONNECT_ACK,
                        CONNECTED,
                        DISCONNECTING,
                    };
                    Allocator *m_allocator;
                    ClientState clientState;

                    void SendProtocolMessage(
                        Crt::Optional<Crt::List<EventStreamHeader> >& headers,
                        Crt::Optional<ByteBuf>& payload,
                        MessageType messageType,
                        uint32_t flags,
                        OnMessageFlush onMessageFlushCallback
                    ) noexcept;

                    static void s_onConnectionShutdown(
                        struct aws_event_stream_rpc_client_connection *connection,
                        int errorCode,
                        void *userData
                    );
                    static void s_onConnectionSetup(
                        struct aws_event_stream_rpc_client_connection *connection,
                        int errorCode,
                        void *userData
                    );
                    static void s_onProtocolMessage(
                        struct aws_event_stream_rpc_client_connection *connection,
                        const struct aws_event_stream_rpc_message_args *messageArgs,
                        void *userData
                    );

                    static void s_protocolMessageCallback(
                    int errorCode, void *userData  
                    ) noexcept;
                    static void s_sendProtocolMessage(
                        std::weak_ptr<EventstreamRpcConnection> connection,
                        Crt::Optional<Crt::List<EventStreamHeader> >& headers,
                        Crt::Optional<ByteBuf>& payload,
                        MessageType messageType,
                        uint32_t flags,
                        OnMessageFlush onMessageFlushCallback
                    ) noexcept;

                    static void s_sendPing(
                        std::weak_ptr<EventstreamRpcConnection> connection,
                        Crt::Optional<Crt::List<EventStreamHeader> >& headers,
                        Crt::Optional<ByteBuf>& payload,
                        OnMessageFlush onMessageFlushCallback
                    ) noexcept;

                    static void s_sendPingResponse(
                        std::weak_ptr<EventstreamRpcConnection> connection,
                        Crt::Optional<Crt::List<EventStreamHeader> >& headers,
                        Crt::Optional<ByteBuf>& payload,
                        OnMessageFlush onMessageFlushCallback
                    ) noexcept;
            };
        }
    }
}
