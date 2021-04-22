/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/event-stream/EventStream.h>

#include <aws/crt/Api.h>
#include <aws/crt/Config.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/auth/Sigv4Signing.h>

#include <algorithm>

namespace Aws
{
    namespace Crt
    {
        namespace Eventstream
        {
            /* This exists to handle aws_event_stream_rpc_client_connection's shutdown callback, which might fire after
             * EventstreamRpcConnection has been destroyed. */
            struct ConnectionCallbackData
            {
                explicit ConnectionCallbackData(Allocator *allocator) : allocator(allocator) {}
                std::shared_ptr<EventstreamRpcConnection> connection;
                Allocator *allocator;
                OnConnect onConnect;
                OnDisconnect onDisconnect;
                OnError onError;
                OnPing onPing;
                ConnectMessageAmender connectMessageAmender;
            };

            struct ProtocolMessageCallbackData
            {
                explicit ProtocolMessageCallbackData(Allocator *allocator) : allocator(allocator) {}
                std::weak_ptr<EventstreamRpcConnection> connection;
                Allocator *allocator;
                OnMessageFlush onMessageFlush;
            };

            EventStreamHeader::EventStreamHeader(const struct aws_event_stream_header_value_pair& header)
            : m_underlyingHandle(header)
            {
            }

            MessageAmendment::MessageAmendment(const ByteBuf& payload)
            noexcept : m_headers(), m_payload(payload)
            {
            }

            MessageAmendment::MessageAmendment(const Crt::List<EventStreamHeader>& headers)
            noexcept : m_headers(headers), m_payload()
            {
            }

            MessageAmendment::MessageAmendment(const Crt::Optional<Crt::List<EventStreamHeader> >& headers, const Crt::Optional<ByteBuf>& payload)
            noexcept : m_headers(headers), m_payload(payload)
            {
            }

            Crt::Optional<Crt::List<EventStreamHeader> > &MessageAmendment::GetHeaders()
            noexcept
            {
                return m_headers;
            }

            Crt::Optional<ByteBuf> &MessageAmendment::GetPayload()
            noexcept
            {
                return m_payload;
            }

            EventstreamRpcConnectionOptions::EventstreamRpcConnectionOptions()
            : Bootstrap(nullptr), SocketOptions(), TlsOptions(), HostName(), Port(0),
              OnDisconnectCallback(), OnErrorCallback(), OnPingCallback(),
              ConnectMessageAmenderCallback()
            {
            }

            EventstreamRpcConnection::EventstreamRpcConnection(
                struct aws_event_stream_rpc_client_connection *connection,
                Allocator *allocator
            ) noexcept : m_underlyingConnection(connection), m_allocator(allocator)
            {
            }

            class UnmanagedConnection final : public EventstreamRpcConnection
            {
              public:
                UnmanagedConnection(struct aws_event_stream_rpc_client_connection *connection, Allocator *allocator)
                    : EventstreamRpcConnection(connection, allocator)
                {
                }

                ~UnmanagedConnection() override
                {
                    if (m_underlyingConnection)
                    {
                        aws_event_stream_rpc_client_connection_release(m_underlyingConnection);
                        m_underlyingConnection = nullptr;
                    }
                }
            };

            bool EventstreamRpcConnection::CreateConnection(
                const EventstreamRpcConnectionOptions& connectionOptions,
                Allocator *allocator
            ) noexcept
            {
                AWS_FATAL_ASSERT(connectionOptions.OnConnectCallback);
                AWS_FATAL_ASSERT(connectionOptions.OnDisconnectCallback);
                AWS_FATAL_ASSERT(connectionOptions.OnErrorCallback);

                auto *callbackData = New<ConnectionCallbackData>(allocator, allocator);
                callbackData->allocator = allocator;
                callbackData->connection = nullptr;
                callbackData->onConnect = connectionOptions.OnConnectCallback;
                callbackData->onDisconnect = connectionOptions.OnDisconnectCallback;
                callbackData->onError = connectionOptions.OnErrorCallback;
                callbackData->onPing = connectionOptions.OnPingCallback;
                callbackData->connectMessageAmender = connectionOptions.ConnectMessageAmenderCallback;

                struct aws_event_stream_rpc_client_connection_options connOptions;
                AWS_ZERO_STRUCT(connOptions);
                connOptions.host_name = connectionOptions.HostName.c_str();
                connOptions.port = connectionOptions.Port;
                connOptions.socket_options = &connectionOptions.SocketOptions.GetImpl();
                connOptions.bootstrap = connectionOptions.Bootstrap->GetUnderlyingHandle();
                connOptions.on_connection_setup = EventstreamRpcConnection::s_onConnectionSetup;
                connOptions.on_connection_protocol_message = EventstreamRpcConnection::s_onProtocolMessage;
                connOptions.on_connection_shutdown = EventstreamRpcConnection::s_onConnectionShutdown;
                connOptions.user_data = callbackData;

                if (connectionOptions.TlsOptions.has_value())
                {
                    connOptions.tls_options = connectionOptions.TlsOptions->GetUnderlyingHandle();
                }

                if (aws_event_stream_rpc_client_connection_connect(allocator, &connOptions))
                {
                    Delete(callbackData, allocator);
                    return false;
                }

                return true;
            }

            aws_event_stream_header_value_pair * EventStreamHeader::GetUnderlyingHandle()
            {
                return &m_underlyingHandle;
            }

            void EventstreamRpcConnection::SendPing(
                Crt::Optional<Crt::List<EventStreamHeader> >& headers,
                Crt::Optional<ByteBuf>& payload,
                OnMessageFlush onMessageFlushCallback
            ) noexcept
            {
                s_sendPing(shared_from_this(), headers, payload, onMessageFlushCallback);
            }

            void EventstreamRpcConnection::SendPingResponse(
                Crt::Optional<Crt::List<EventStreamHeader> >& headers,
                Crt::Optional<ByteBuf>& payload,
                OnMessageFlush onMessageFlushCallback
            ) noexcept
            {
                s_sendPingResponse(shared_from_this(), headers, payload, onMessageFlushCallback);
            }

            void EventstreamRpcConnection::s_sendPing(
                std::weak_ptr<EventstreamRpcConnection> connection,
                Crt::Optional<Crt::List<EventStreamHeader> >& headers,
                Crt::Optional<ByteBuf>& payload,
                OnMessageFlush onMessageFlushCallback
            ) noexcept
            {
                s_sendProtocolMessage(connection, headers, payload, AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT,0,onMessageFlushCallback);
            }

            void EventstreamRpcConnection::s_sendPingResponse(
                std::weak_ptr<EventstreamRpcConnection> connection,
                Crt::Optional<Crt::List<EventStreamHeader> >& headers,
                Crt::Optional<ByteBuf>& payload,
                OnMessageFlush onMessageFlushCallback
            ) noexcept
            {
                s_sendProtocolMessage(connection, headers, payload, AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT_ACK,0,onMessageFlushCallback);
            }

            void EventstreamRpcConnection::SendProtocolMessage(
                Crt::Optional<Crt::List<EventStreamHeader> >& headers,
                Crt::Optional<ByteBuf>& payload,
                MessageType messageType,
                uint32_t flags,
                OnMessageFlush onMessageFlushCallback
            ) noexcept
            {
                s_sendProtocolMessage(shared_from_this(), headers, payload, messageType, flags, onMessageFlushCallback);
            }

            void EventstreamRpcConnection::s_protocolMessageCallback(
              int errorCode, void *userData  
            ) noexcept
            {
                auto *callbackData = static_cast<ProtocolMessageCallbackData *>(userData);

                /* Only close the connection if it hasn't expired already. */
                if (auto connectionPtr = callbackData->connection.lock())
                {
                    connectionPtr->Close(errorCode);
                }
                /* Call the user-provided callback. */
                if(callbackData->onMessageFlush) callbackData->onMessageFlush(errorCode);

                Delete(callbackData, callbackData->allocator);
            }

            void EventstreamRpcConnection::s_sendProtocolMessage(
                std::weak_ptr<EventstreamRpcConnection> connection,
                Crt::Optional<Crt::List<EventStreamHeader> >& headers,
                Crt::Optional<ByteBuf>& payload,
                MessageType messageType,
                uint32_t flags,
                OnMessageFlush onMessageFlushCallback
            ) noexcept
            {
                struct aws_event_stream_rpc_message_args msg_args;
                AWS_ZERO_STRUCT(msg_args);
                struct aws_array_list headersArray;
                AWS_ZERO_STRUCT(headersArray);

                /* Check if the connection has expired before attempting to send. */
                if (auto connectionPtr = connection.lock())
                {
                    if(aws_event_stream_headers_list_init(&headersArray, connectionPtr->m_allocator)) {
                    onMessageFlushCallback(AWS_OP_ERR);
                    } else {
                        msg_args.message_flags = flags;
                        msg_args.message_type = messageType;
                        
                        /* Populate the array with the underlying handle of each EventStreamHeader. */
                        if (headers.has_value()) {
                            for (auto& i : headers.value()) {
                                aws_array_list_push_back(&headersArray, i.GetUnderlyingHandle());
                            }

                            msg_args.headers = (struct aws_event_stream_header_value_pair *) headersArray.data;
                            msg_args.headers_count = aws_array_list_length(&headersArray);
                        }
                        if (payload.has_value()) {
                            msg_args.payload = &payload.value();
                        }

                        auto *callbackData = New<ProtocolMessageCallbackData>(connectionPtr->m_allocator, connectionPtr->m_allocator);
                        callbackData->connection = connectionPtr;
                        callbackData->onMessageFlush = onMessageFlushCallback;

                        if (aws_event_stream_rpc_client_connection_send_protocol_message(
                                connectionPtr->m_underlyingConnection, &msg_args, s_protocolMessageCallback, callbackData)) {
                        }
                    }
                }

                if (aws_array_list_is_valid(&headersArray)) {
                    aws_event_stream_headers_list_cleanup(&headersArray);
                }
            }

            void EventstreamRpcConnection::Close() noexcept 
            {
                aws_event_stream_rpc_client_connection_close(this->m_underlyingConnection, AWS_OP_SUCCESS);
            }

            void EventstreamRpcConnection::Close(int errorCode) noexcept 
            {
                aws_event_stream_rpc_client_connection_close(this->m_underlyingConnection, errorCode);
            }

            EventStreamHeader::EventStreamHeader(const String& name, const String& value)
            {
                m_underlyingHandle.header_name_len = (uint8_t)name.length();
                (void) strncpy(m_underlyingHandle.header_name, name.c_str(), std::min((int)name.length(), INT8_MAX));
                m_underlyingHandle.header_value_type = AWS_EVENT_STREAM_HEADER_STRING;
                valueByteBuf = Crt::ByteBufFromCString(value.c_str());
                m_underlyingHandle.header_value.variable_len_val = valueByteBuf.buffer;
                m_underlyingHandle.header_value_len = (uint16_t)valueByteBuf.len;
            }

            bool EventStreamHeader::operator==(const EventStreamHeader &other) const noexcept
            {
                // TODO: Write lowercase strncmp
                return (
                    other.m_underlyingHandle.header_name_len == m_underlyingHandle.header_name_len &&
                    strncmp(other.m_underlyingHandle.header_name, m_underlyingHandle.header_name, m_underlyingHandle.header_name_len)
                );
            }

            void EventstreamRpcConnection::s_onConnectionSetup(
                struct aws_event_stream_rpc_client_connection *connection,
                int errorCode,
                void *userData
            )
            {
                /**
                 * Allocate an EventstreamRpcConnection and seat it to `ConnectionCallbackData`'s shared_ptr.
                 */
                auto *callbackData = static_cast<ConnectionCallbackData *>(userData);

                if(!errorCode)
                {
                    /* Connection is established. */
                    auto connectionObj = std::allocate_shared<UnmanagedConnection>(
                        Aws::Crt::StlAllocator<UnmanagedConnection>(), connection, callbackData->allocator); 
                    if (connectionObj)
                    {
                        Crt::List<EventStreamHeader> defaultHeaders{EventStreamHeader(String(":version"), String("0.1.0"))};
                        MessageAmendment messageAmendment(defaultHeaders);
                        if(callbackData->connectMessageAmender)
                        {
                            MessageAmendment connectAmendment = callbackData->connectMessageAmender();
                            if(connectAmendment.GetHeaders().has_value()) {
                                for (auto& connectHeader : connectAmendment.GetHeaders().value()) {
                                    auto& defaultHeaderList = messageAmendment.GetHeaders().value();
                                    /* Existing headers should not be overwritten. */
                                    if(std::find(defaultHeaderList.begin(), defaultHeaderList.end(), connectHeader) == defaultHeaderList.end()){
                                        messageAmendment.GetHeaders().value().push_back(connectHeader);
                                    }
                                }
                            }
                        }
                        callbackData->connection = connectionObj;
                        /* Send a CONNECT packet to the server. */
                        s_sendPing(connectionObj, messageAmendment.GetHeaders(), messageAmendment.GetPayload(), nullptr);
                        connectionObj->clientState = WAITING_FOR_CONNECT_ACK;
                        return;
                    }

                    /* Release if we're unable to allocate the connection's containing object. */
                    aws_event_stream_rpc_client_connection_release(connection);
                    errorCode = aws_last_error();
                }

                callbackData->connection->clientState = DISCONNECTED;
                callbackData->onError(errorCode);
                callbackData->connection->Close(errorCode);
                Delete(callbackData, callbackData->allocator);
            }

            void EventstreamRpcConnection::s_onConnectionShutdown(
                struct aws_event_stream_rpc_client_connection *connection,
                int errorCode,
                void *userData
            )
            {
                (void)connection;
                auto *callbackData = static_cast<ConnectionCallbackData *>(userData);

                /* Don't invoke callback if the connection object has expired. */
                callbackData->onDisconnect(callbackData->connection, errorCode);

                Delete(callbackData, callbackData->allocator);
            }

            void EventstreamRpcConnection::s_onProtocolMessage(
                struct aws_event_stream_rpc_client_connection *connection,
                const struct aws_event_stream_rpc_message_args *messageArgs,
                void *userData
            )
            {
                AWS_FATAL_ASSERT(messageArgs);
                (void) connection;

                auto *callbackData = static_cast<ConnectionCallbackData *>(userData);
                std::shared_ptr<EventstreamRpcConnection> connectionObj = callbackData->connection;

                /* TODO: Make a Crt::List<EventStreamHeader> from each of the headers. */
                for (size_t i = 0; i < messageArgs->headers_count; ++i) {
                    //struct aws_event_stream_header_value_pair *header = &messageArgs->headers[i];
                }

                switch(messageArgs->message_type) {
                    case AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_CONNECT_ACK:
                        if (connectionObj->clientState == WAITING_FOR_CONNECT_ACK)
                        {
                            if(messageArgs->message_flags & AWS_EVENT_STREAM_RPC_MESSAGE_FLAG_CONNECTION_ACCEPTED)
                            {
                                connectionObj->clientState = CONNECTED;
                            }
                            else
                            {
                                connectionObj->clientState = DISCONNECTING;
                                connectionObj->Close();
                            }
                        }
                        callbackData->onConnect(std::move(connectionObj));
                    case AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_PING:
                        break;
                    case AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_PING_RESPONSE:
                        return;
                        break;
                    case AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_PROTOCOL_ERROR:
                    case AWS_EVENT_STREAM_RPC_MESSAGE_TYPE_INTERNAL_ERROR:
                        callbackData->onError(AWS_ERROR_EVENT_STREAM_RPC_PROTOCOL_ERROR);
                        connectionObj->Close(AWS_ERROR_EVENT_STREAM_RPC_PROTOCOL_ERROR);
                        break;
                    default:
                        return;
                        break;
                }
                
                //ByteCursor payload = ByteCursorFromByteBuf(*messageArgs->payload);
            }
        }
    }
}
