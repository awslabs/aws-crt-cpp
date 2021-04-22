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
#include <aws/crt/io/SocketOptions.h>
#include <aws/crt/io/TlsOptions.h>

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
            class EventstreamRpcClient;
            class EventstreamRpcConnection;

            using HeaderType = aws_event_stream_header_value_type;

            class AWS_CRT_CPP_API EventStreamHeader final
            {
                public:
                    template <typename T>
                    EventStreamHeader(const String& name, T& value);
                    EventStreamHeader(const String& name, bool value);
                    EventStreamHeader(const String& name, int8_t value);
                    EventStreamHeader(const String& name, int16_t value);
                    EventStreamHeader(const String& name, int32_t value);
                    EventStreamHeader(const String& name, int64_t value);
                    EventStreamHeader(const String& name, DateTime& value);
                    EventStreamHeader(const String& name, ByteBuf value);
                    EventStreamHeader(const String& name, Crt::UUID value);

                    HeaderType GetHeaderType();
                    HeaderType GetHeaderValue(T& value);
                    String& GetHeaderName();
                    bool& GetValueAsBoolean();
                    int8_t& GetValueAsByte();
                    int16_t& GetValueAsShort();
                    int32_t& GetValueAsInt();
                    int64_t& GetValueAsLong();
                    DateTime& GetValueAsTimestamp();
                    ByteBuf& GetValueAsBytes();
                    Crt::UUID& GetValueAsUUID();

                    void SetValue(T& value, HeaderType headerType);
                    void SetValue(bool value);
                    void SetValue(int8_t value);
                    void SetValue(int16_t value);
                    void SetValue(int32_t value);
                    void SetValue(int64_t value);
                    void SetValue(DateTime& value);
                    void SetValue(ByteBuf value);
                    void SetValue(Crt::UUID value);

                    bool operator==(const EventStreamHeader &other) const noexcept;
                private:
                    String m_headerName;
                    HeaderType m_headerType;
                    ByteBuf m_headerValue; 
            };

            class AWS_CRT_CPP_API MessageAmendInfo final
            {
                public:
                    MessageAmendInfo(Crt::List<EventStreamHeader> headers, ByteBuf payload);
                    Crt::List<EventStreamHeader> &GetEventStreamHeaders() const noexcept;
                    ByteBuf &GetPayload() const noexcept;
                private:
                    Crt::List<EventStreamHeader> m_headers;
                    ByteBuf m_payload;
            };

            using MessageType = aws_event_stream_rpc_message_type;
            using MessageFlag = aws_event_stream_rpc_message_flag;

            class AWS_CRT_CPP_API EventstreamRpcClient
            {
                public:
                    EventstreamRpcClient(EventstreamRpcConnection& connection);
                private:
                    EventstreamRpcConnection m_connection;
            };

            class AWS_CRT_CPP_API LifecycleHandler
            {
                public:
                    virtual void OnConnect() = 0;
                    virtual void OnDisconnect(int errorCode) = 0;
                    virtual bool OnError(int errorCode) = 0;
                    virtual void OnPing(Crt::List<EventStreamHeader> headers, ByteBuf payload) = 0;
            };

            using ConnectMessageAmender = std::function<MessageAmendInfo(void)>;

            class AWS_CRT_CPP_API EventstreamRpcConnectionOptions final
            {
                public:
                    EventstreamRpcConnectionOptions(
                        Allocator *allocator,                        // Should out live this object
                        Io::ClientBootstrap *clientBootstrap,   // Should out live this object
                        Io::EventLoopGroup &eventLoopGroup,
                        const Io::SocketOptions &socketOptions, // Make a copy and save in this object
                        Crt::Io::TlsContext &&tlsContext,
                        const char *hostName, // Make a copy and save in this object
                        uint16_t port,       // Make a copy and save in this object
                        ConnectMessageAmender connectMessageAmender
                    );
                private:
                    String m_hostName;
                    uint16_t m_port;
                    Io::TlsContext m_tlsContext;
                    Io::SocketOptions m_socketOptions;
                    ConnectMessageAmender m_connectMessageAmender;
            };

            class AWS_CRT_CPP_API EventstreamRpcConnection final
            {
                friend class EventstreamRpcClient;

                public:
                    ~EventstreamRpcConnection();
                    EventstreamRpcConnection(const EventstreamRpcConnection &) = delete;
                    EventstreamRpcConnection(EventstreamRpcConnection &&) = delete;
                    EventstreamRpcConnection &operator=(const EventstreamRpcConnection &) = delete;
                    EventstreamRpcConnection &operator=(EventstreamRpcConnection &&) = delete;
                    /**
                     * @return true if the instance is in a valid state, false otherwise.
                    */
                    operator bool() const noexcept;
                    /**
                     * @return the value of the last aws error encountered by operations on this instance.
                    */
                    int LastError() const noexcept;

                    std::future<void> SendPing(Crt::Optional<MessageAmendInfo> pingData) const;
                    std::future<void> SendPingResponse(Crt::Optional<MessageAmendInfo> pingResponseData) const;

                    std::future<void> Connect(const LifecycleHandler& lifecycleHandler);
                    std::future<void> Disconnect() const;
                private:
                    struct aws_event_stream_rpc_client_connection *m_underlyingConnection;
                    Io::TlsConnectionOptions m_tlsOptions;

                    EventstreamRpcConnection(
                        Allocator *allocator,                        // Should out live this object
                        Io::ClientBootstrap *clientBootstrap,   // Should out live this object
                        const Io::SocketOptions &socketOptions, // Make a copy and save in this object
                        const Io::TlsConnectionOptions &connectionOptions,
                        const char *hostName, // Make a copy and save in this object
                        uint16_t port       // Make a copy and save in this object
                    );

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
            };

            class AWS_CRT_CPP_API StreamResponseHandler
            {
                public:
                    virtual void OnStreamEvent(StreamEventType streamEvent) = 0;
                    virtual bool OnStreamError(int errorCode) = 0;
                    virtual void OnStreamClosed() = 0;
            };
        }
    }
}
