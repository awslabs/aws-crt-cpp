/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/mqtt/MqttClient.h>

#include <aws/crt/StlAllocator.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/Bootstrap.h>

#include <utility>

namespace Aws
{
    namespace Crt
    {
        namespace Mqtt
        {
            void MqttConnection::s_onConnectionInterrupted(aws_mqtt_client_connection *, int errorCode, void *userData)
            {
                auto connWrapper = reinterpret_cast<MqttConnection *>(userData);
                if (connWrapper->OnConnectionInterrupted)
                {
                    connWrapper->OnConnectionInterrupted(*connWrapper, errorCode);
                }
            }

            void MqttConnection::s_onConnectionResumed(
                aws_mqtt_client_connection *,
                ReturnCode returnCode,
                bool sessionPresent,
                void *userData)
            {
                auto connWrapper = reinterpret_cast<MqttConnection *>(userData);
                if (connWrapper->OnConnectionResumed)
                {
                    connWrapper->OnConnectionResumed(*connWrapper, returnCode, sessionPresent);
                }
            }

            void MqttConnection::s_onConnectionCompleted(
                aws_mqtt_client_connection *,
                int errorCode,
                enum aws_mqtt_connect_return_code returnCode,
                bool sessionPresent,
                void *userData)
            {
                auto connWrapper = reinterpret_cast<MqttConnection *>(userData);
                if (connWrapper->OnConnectionCompleted)
                {
                    connWrapper->OnConnectionCompleted(*connWrapper, errorCode, returnCode, sessionPresent);
                }
            }

            void MqttConnection::s_onDisconnect(aws_mqtt_client_connection *, void *userData)
            {
                auto connWrapper = reinterpret_cast<MqttConnection *>(userData);
                if (connWrapper->OnDisconnect)
                {
                    connWrapper->OnDisconnect(*connWrapper);
                }
            }

            struct PubCallbackData
            {
                PubCallbackData() : connection(nullptr), allocator(nullptr) {}

                MqttConnection *connection;
                OnPublishReceivedHandler onPublishReceived;
                Allocator *allocator;
            };

            static void s_cleanUpOnPublishData(void *userData)
            {
                auto callbackData = reinterpret_cast<PubCallbackData *>(userData);
                Crt::Delete(callbackData, callbackData->allocator);
            }

            void MqttConnection::s_onPublish(
                aws_mqtt_client_connection *,
                const aws_byte_cursor *topic,
                const aws_byte_cursor *payload,
                void *userData)
            {
                auto callbackData = reinterpret_cast<PubCallbackData *>(userData);

                if (callbackData->onPublishReceived)
                {
                    String topicStr(reinterpret_cast<char *>(topic->ptr), topic->len);
                    ByteBuf payloadBuf = aws_byte_buf_from_array(payload->ptr, payload->len);
                    callbackData->onPublishReceived(*(callbackData->connection), topicStr, payloadBuf);
                }
            }

            struct OpCompleteCallbackData
            {
                OpCompleteCallbackData() : connection(nullptr), topic(nullptr), allocator(nullptr) {}

                MqttConnection *connection;
                OnOperationCompleteHandler onOperationComplete;
                const char *topic;
                Allocator *allocator;
            };

            void MqttConnection::s_onOpComplete(
                aws_mqtt_client_connection *,
                uint16_t packetId,
                int errorCode,
                void *userData)
            {
                auto callbackData = reinterpret_cast<OpCompleteCallbackData *>(userData);

                if (callbackData->onOperationComplete)
                {
                    callbackData->onOperationComplete(*callbackData->connection, packetId, errorCode);
                }

                if (callbackData->topic)
                {
                    aws_mem_release(
                        callbackData->allocator, reinterpret_cast<void *>(const_cast<char *>(callbackData->topic)));
                }

                Crt::Delete(callbackData, callbackData->allocator);
            }

            struct SubAckCallbackData
            {
                SubAckCallbackData() : connection(nullptr), topic(nullptr), allocator(nullptr) {}

                MqttConnection *connection;
                OnSubAckHandler onSubAck;
                const char *topic;
                Allocator *allocator;
            };

            void MqttConnection::s_onSubAck(
                aws_mqtt_client_connection *,
                uint16_t packetId,
                const struct aws_byte_cursor *topic,
                enum aws_mqtt_qos qos,
                int errorCode,
                void *userData)
            {
                auto callbackData = reinterpret_cast<SubAckCallbackData *>(userData);

                if (callbackData->onSubAck)
                {
                    String topicStr(reinterpret_cast<char *>(topic->ptr), topic->len);
                    callbackData->onSubAck(*callbackData->connection, packetId, topicStr, qos, errorCode);
                }

                if (callbackData->topic)
                {
                    aws_mem_release(
                        callbackData->allocator, reinterpret_cast<void *>(const_cast<char *>(callbackData->topic)));
                }

                Crt::Delete(callbackData, callbackData->allocator);
            }

            struct MultiSubAckCallbackData
            {
                MultiSubAckCallbackData() : connection(nullptr), topic(nullptr), allocator(nullptr) {}

                MqttConnection *connection;
                OnMultiSubAckHandler onSubAck;
                const char *topic;
                Allocator *allocator;
            };

            void MqttConnection::s_onMultiSubAck(
                aws_mqtt_client_connection *,
                uint16_t packetId,
                const struct aws_array_list *topicSubacks,
                int errorCode,
                void *userData)
            {
                auto callbackData = reinterpret_cast<MultiSubAckCallbackData *>(userData);

                if (callbackData->onSubAck)
                {
                    size_t length = aws_array_list_length(topicSubacks);
                    Vector<String> topics(length);
                    QOS qos = AWS_MQTT_QOS_AT_MOST_ONCE;
                    for (size_t i = 0; i < length; ++i)
                    {
                        aws_mqtt_topic_subscription *subscription = NULL;
                        aws_array_list_get_at_ptr(topicSubacks, reinterpret_cast<void **>(&subscription), i);
                        topics.push_back(
                            String(reinterpret_cast<char *>(subscription->topic.ptr), subscription->topic.len));
                        qos = subscription->qos;
                    }

                    callbackData->onSubAck(*callbackData->connection, packetId, topics, qos, errorCode);
                }

                if (callbackData->topic)
                {
                    aws_mem_release(
                        callbackData->allocator, reinterpret_cast<void *>(const_cast<char *>(callbackData->topic)));
                }

                Crt::Delete(callbackData, callbackData->allocator);
            }

            void MqttConnection::s_connectionInit(
                MqttConnection *self,
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions)
            {

                self->m_hostName = String(hostName);
                self->m_port = port;
                self->m_socketOptions = socketOptions;

                self->m_underlyingConnection = aws_mqtt_client_connection_new(self->m_owningClient);

                if (self->m_underlyingConnection)
                {
                    aws_mqtt_client_connection_set_connection_interruption_handlers(
                        self->m_underlyingConnection,
                        MqttConnection::s_onConnectionInterrupted,
                        self,
                        MqttConnection::s_onConnectionResumed,
                        self);
                }
            }

            void MqttConnection::s_onWebsocketHandshake(
                struct aws_http_message *rawRequest,
                void *user_data,
                aws_mqtt_transform_websocket_handshake_complete_fn *complete_fn,
                void *complete_ctx)
            {
                auto connection = reinterpret_cast<MqttConnection *>(user_data);

                Allocator *allocator = connection->m_owningClient->allocator;
                // we have to do this because of private constructors.
                auto toSeat =
                    reinterpret_cast<Http::HttpRequest *>(aws_mem_acquire(allocator, sizeof(Http::HttpRequest)));
                toSeat = new (toSeat) Http::HttpRequest(allocator, rawRequest);

                std::shared_ptr<Http::HttpRequest> request = std::shared_ptr<Http::HttpRequest>(
                    toSeat, [allocator](Http::HttpRequest *ptr) { Crt::Delete(ptr, allocator); });

                auto onInterceptComplete =
                    [complete_fn,
                     complete_ctx](const std::shared_ptr<Http::HttpRequest> &transformedRequest, int errorCode) {
                        complete_fn(transformedRequest->GetUnderlyingMessage(), errorCode, complete_ctx);
                    };

                connection->WebsocketInterceptor(request, onInterceptComplete);
            }

            MqttConnection::MqttConnection(
                aws_mqtt_client *client,
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                const Crt::Io::TlsContext &tlsContext,
                bool useWebsocket) noexcept
                : m_owningClient(client), m_tlsContext(tlsContext), m_tlsOptions(tlsContext.NewConnectionOptions()),
                  m_onAnyCbData(nullptr), m_useTls(true), m_useWebsocket(useWebsocket)
            {
                s_connectionInit(this, hostName, port, socketOptions);
            }

            MqttConnection::MqttConnection(
                aws_mqtt_client *client,
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                bool useWebsocket) noexcept
                : m_owningClient(client), m_onAnyCbData(nullptr), m_useTls(false), m_useWebsocket(useWebsocket)
            {
                s_connectionInit(this, hostName, port, socketOptions);
            }

            MqttConnection::~MqttConnection()
            {
                if (*this)
                {
                    aws_mqtt_client_connection_destroy(m_underlyingConnection);

                    if (m_onAnyCbData)
                    {
                        auto pubCallbackData = reinterpret_cast<PubCallbackData *>(m_onAnyCbData);
                        Crt::Delete(pubCallbackData, pubCallbackData->allocator);
                    }
                }
            }

            MqttConnection::operator bool() const noexcept { return m_underlyingConnection != nullptr; }

            int MqttConnection::LastError() const noexcept { return aws_last_error(); }

            bool MqttConnection::SetWill(const char *topic, QOS qos, bool retain, const ByteBuf &payload) noexcept
            {
                ByteBuf topicBuf = aws_byte_buf_from_c_str(topic);
                ByteCursor topicCur = aws_byte_cursor_from_buf(&topicBuf);
                ByteCursor payloadCur = aws_byte_cursor_from_buf(&payload);

                return aws_mqtt_client_connection_set_will(
                           m_underlyingConnection, &topicCur, qos, retain, &payloadCur) == 0;
            }

            bool MqttConnection::SetLogin(const char *userName, const char *password) noexcept
            {
                ByteBuf userNameBuf = aws_byte_buf_from_c_str(userName);
                ByteCursor userNameCur = aws_byte_cursor_from_buf(&userNameBuf);

                ByteCursor *pwdCurPtr = nullptr;
                ByteCursor pwdCur;

                if (password)
                {
                    pwdCur = ByteCursorFromCString(password);
                    pwdCurPtr = &pwdCur;
                }
                return aws_mqtt_client_connection_set_login(m_underlyingConnection, &userNameCur, pwdCurPtr) == 0;
            }

            bool MqttConnection::SetWebsocketProxyOptions(
                const Http::HttpClientConnectionProxyOptions &proxyOptions) noexcept
            {
                m_proxyOptions = proxyOptions;
                return true;
            }

            bool MqttConnection::Connect(
                const char *clientId,
                bool cleanSession,
                uint16_t keepAliveTime,
                uint32_t requestTimeoutMs) noexcept
            {
                aws_mqtt_connection_options options;
                AWS_ZERO_STRUCT(options);
                options.client_id = aws_byte_cursor_from_c_str(clientId);
                options.host_name = aws_byte_cursor_from_array(
                    reinterpret_cast<const uint8_t *>(m_hostName.data()), m_hostName.length());
                options.tls_options =
                    m_useTls ? const_cast<aws_tls_connection_options *>(m_tlsOptions.GetUnderlyingHandle()) : nullptr;
                options.port = m_port;
                options.socket_options = &m_socketOptions.GetImpl();
                options.clean_session = cleanSession;
                options.keep_alive_time_secs = keepAliveTime;
                options.ping_timeout_ms = requestTimeoutMs;
                options.on_connection_complete = MqttConnection::s_onConnectionCompleted;
                options.user_data = this;

                if (m_useWebsocket)
                {
                    if (WebsocketInterceptor)
                    {
                        if (aws_mqtt_client_connection_use_websockets(
                                m_underlyingConnection, MqttConnection::s_onWebsocketHandshake, this, nullptr, nullptr))
                        {
                            return false;
                        }
                    }
                    else
                    {
                        if (aws_mqtt_client_connection_use_websockets(
                                m_underlyingConnection, nullptr, nullptr, nullptr, nullptr))
                        {
                            return false;
                        }
                    }

                    if (m_proxyOptions)
                    {
                        struct aws_http_proxy_options proxyOptions;
                        AWS_ZERO_STRUCT(proxyOptions);

                        if (!m_proxyOptions->BasicAuthUsername.empty())
                        {
                            proxyOptions.auth_username =
                                ByteCursorFromCString(m_proxyOptions->BasicAuthUsername.c_str());
                        }

                        if (!m_proxyOptions->BasicAuthPassword.empty())
                        {
                            proxyOptions.auth_password =
                                ByteCursorFromCString(m_proxyOptions->BasicAuthPassword.c_str());
                        }

                        if (m_proxyOptions->TlsOptions)
                        {
                            proxyOptions.tls_options = const_cast<struct aws_tls_connection_options *>(
                                m_proxyOptions->TlsOptions->GetUnderlyingHandle());
                        }

                        proxyOptions.auth_type =
                            static_cast<enum aws_http_proxy_authentication_type>(m_proxyOptions->AuthType);
                        proxyOptions.host = ByteCursorFromCString(m_proxyOptions->HostName.c_str());
                        proxyOptions.port = m_proxyOptions->Port;

                        if (aws_mqtt_client_connection_set_websocket_proxy_options(
                                m_underlyingConnection, &proxyOptions))
                        {
                            return false;
                        }
                    }
                }

                return aws_mqtt_client_connection_connect(m_underlyingConnection, &options) == AWS_OP_SUCCESS;
            }

            bool MqttConnection::Disconnect() noexcept
            {
                return aws_mqtt_client_connection_disconnect(
                           m_underlyingConnection, MqttConnection::s_onDisconnect, this) == AWS_OP_SUCCESS;
            }

            bool MqttConnection::SetOnMessageHandler(OnPublishReceivedHandler &&onPublish) noexcept
            {
                auto pubCallbackData = Aws::Crt::New<PubCallbackData>(m_owningClient->allocator);

                if (!pubCallbackData)
                {
                    return false;
                }

                pubCallbackData->connection = this;
                pubCallbackData->onPublishReceived = std::move(onPublish);
                pubCallbackData->allocator = m_owningClient->allocator;

                if (!aws_mqtt_client_connection_set_on_any_publish_handler(
                        m_underlyingConnection, s_onPublish, pubCallbackData))
                {
                    m_onAnyCbData = reinterpret_cast<void *>(pubCallbackData);
                    return true;
                }

                Aws::Crt::Delete(pubCallbackData, pubCallbackData->allocator);
                return false;
            }

            uint16_t MqttConnection::Subscribe(
                const char *topicFilter,
                QOS qos,
                OnPublishReceivedHandler &&onPublish,
                OnSubAckHandler &&onSubAck) noexcept
            {

                auto pubCallbackData = Crt::New<PubCallbackData>(m_owningClient->allocator);

                if (!pubCallbackData)
                {
                    return 0;
                }

                pubCallbackData->connection = this;
                pubCallbackData->onPublishReceived = std::move(onPublish);
                pubCallbackData->allocator = m_owningClient->allocator;

                auto subAckCallbackData = Crt::New<SubAckCallbackData>(m_owningClient->allocator);

                if (!subAckCallbackData)
                {
                    Crt::Delete(pubCallbackData, m_owningClient->allocator);
                    return 0;
                }

                subAckCallbackData->connection = this;
                subAckCallbackData->allocator = m_owningClient->allocator;
                subAckCallbackData->onSubAck = std::move(onSubAck);
                subAckCallbackData->topic = nullptr;
                subAckCallbackData->allocator = m_owningClient->allocator;

                ByteBuf topicFilterBuf = aws_byte_buf_from_c_str(topicFilter);
                ByteCursor topicFilterCur = aws_byte_cursor_from_buf(&topicFilterBuf);

                uint16_t packetId = aws_mqtt_client_connection_subscribe(
                    m_underlyingConnection,
                    &topicFilterCur,
                    qos,
                    s_onPublish,
                    pubCallbackData,
                    s_cleanUpOnPublishData,
                    s_onSubAck,
                    subAckCallbackData);

                if (!packetId)
                {
                    Crt::Delete(pubCallbackData, pubCallbackData->allocator);
                    Crt::Delete(subAckCallbackData, subAckCallbackData->allocator);
                }

                return packetId;
            }

            uint16_t MqttConnection::Subscribe(
                const Vector<std::pair<const char *, OnPublishReceivedHandler>> &topicFilters,
                QOS qos,
                OnMultiSubAckHandler &&onSubAck) noexcept
            {
                uint16_t packetId = 0;
                auto subAckCallbackData = Crt::New<MultiSubAckCallbackData>(m_owningClient->allocator);

                if (!subAckCallbackData)
                {
                    return 0;
                }

                aws_array_list multiPub;
                AWS_ZERO_STRUCT(multiPub);

                if (aws_array_list_init_dynamic(
                        &multiPub, m_owningClient->allocator, topicFilters.size(), sizeof(aws_mqtt_topic_subscription)))
                {
                    Crt::Delete(subAckCallbackData, m_owningClient->allocator);
                    return 0;
                }

                for (auto &topicFilter : topicFilters)
                {
                    auto pubCallbackData = Crt::New<PubCallbackData>(m_owningClient->allocator);

                    if (!pubCallbackData)
                    {
                        goto clean_up;
                    }

                    pubCallbackData->connection = this;
                    pubCallbackData->onPublishReceived = topicFilter.second;
                    pubCallbackData->allocator = m_owningClient->allocator;

                    ByteBuf topicFilterBuf = aws_byte_buf_from_c_str(topicFilter.first);
                    ByteCursor topicFilterCur = aws_byte_cursor_from_buf(&topicFilterBuf);

                    aws_mqtt_topic_subscription subscription;
                    subscription.on_cleanup = s_cleanUpOnPublishData;
                    subscription.on_publish = s_onPublish;
                    subscription.on_publish_ud = pubCallbackData;
                    subscription.qos = qos;
                    subscription.topic = topicFilterCur;

                    aws_array_list_push_back(&multiPub, reinterpret_cast<const void *>(&subscription));
                }

                subAckCallbackData->connection = this;
                subAckCallbackData->allocator = m_owningClient->allocator;
                subAckCallbackData->onSubAck = std::move(onSubAck);
                subAckCallbackData->topic = nullptr;
                subAckCallbackData->allocator = m_owningClient->allocator;

                packetId = aws_mqtt_client_connection_subscribe_multiple(
                    m_underlyingConnection, &multiPub, s_onMultiSubAck, subAckCallbackData);

            clean_up:
                if (!packetId)
                {
                    size_t length = aws_array_list_length(&multiPub);
                    for (size_t i = 0; i < length; ++i)
                    {
                        aws_mqtt_topic_subscription *subscription = NULL;
                        aws_array_list_get_at_ptr(&multiPub, reinterpret_cast<void **>(&subscription), i);
                        auto pubCallbackData = reinterpret_cast<PubCallbackData *>(subscription->on_publish_ud);
                        Crt::Delete(pubCallbackData, m_owningClient->allocator);
                    }

                    Crt::Delete(subAckCallbackData, m_owningClient->allocator);
                }

                aws_array_list_clean_up(&multiPub);

                return packetId;
            }

            uint16_t MqttConnection::Unsubscribe(
                const char *topicFilter,
                OnOperationCompleteHandler &&onOpComplete) noexcept
            {
                auto opCompleteCallbackData = Crt::New<OpCompleteCallbackData>(m_owningClient->allocator);

                if (!opCompleteCallbackData)
                {
                    return 0;
                }

                opCompleteCallbackData->connection = this;
                opCompleteCallbackData->allocator = m_owningClient->allocator;
                opCompleteCallbackData->onOperationComplete = std::move(onOpComplete);
                opCompleteCallbackData->topic = nullptr;
                ByteBuf topicFilterBuf = aws_byte_buf_from_c_str(topicFilter);
                ByteCursor topicFilterCur = aws_byte_cursor_from_buf(&topicFilterBuf);

                uint16_t packetId = aws_mqtt_client_connection_unsubscribe(
                    m_underlyingConnection, &topicFilterCur, s_onOpComplete, opCompleteCallbackData);

                if (!packetId)
                {
                    Crt::Delete(opCompleteCallbackData, m_owningClient->allocator);
                }

                return packetId;
            }

            uint16_t MqttConnection::Publish(
                const char *topic,
                QOS qos,
                bool retain,
                const ByteBuf &payload,
                OnOperationCompleteHandler &&onOpComplete) noexcept
            {

                auto opCompleteCallbackData = Crt::New<OpCompleteCallbackData>(m_owningClient->allocator);
                if (!opCompleteCallbackData)
                {
                    return 0;
                }

                size_t topicLen = strlen(topic) + 1;
                char *topicCpy =
                    reinterpret_cast<char *>(aws_mem_calloc(m_owningClient->allocator, topicLen, sizeof(char)));

                if (!topicCpy)
                {
                    Crt::Delete(opCompleteCallbackData, m_owningClient->allocator);
                }

                memcpy(topicCpy, topic, topicLen);

                opCompleteCallbackData->connection = this;
                opCompleteCallbackData->allocator = m_owningClient->allocator;
                opCompleteCallbackData->onOperationComplete = std::move(onOpComplete);
                opCompleteCallbackData->topic = topicCpy;
                ByteCursor topicCur = aws_byte_cursor_from_array(topicCpy, topicLen - 1);

                ByteCursor payloadCur = aws_byte_cursor_from_buf(&payload);
                uint16_t packetId = aws_mqtt_client_connection_publish(
                    m_underlyingConnection,
                    &topicCur,
                    qos,
                    retain,
                    &payloadCur,
                    s_onOpComplete,
                    opCompleteCallbackData);

                if (!packetId)
                {
                    aws_mem_release(m_owningClient->allocator, reinterpret_cast<void *>(topicCpy));
                    Crt::Delete(opCompleteCallbackData, m_owningClient->allocator);
                }

                return packetId;
            }

            MqttClient::MqttClient(Io::ClientBootstrap &bootstrap, Allocator *allocator) noexcept : m_client(nullptr)
            {
                m_client =
                    reinterpret_cast<aws_mqtt_client *>(aws_mem_acquire(allocator, sizeof(struct aws_mqtt_client)));
                if (!m_client)
                {
                    return;
                }

                if (aws_mqtt_client_init(m_client, allocator, bootstrap.GetUnderlyingHandle()))
                {
                    aws_mem_release(allocator, reinterpret_cast<void *>(m_client));
                    m_client = nullptr;
                }
            }

            MqttClient::~MqttClient()
            {
                if (m_client)
                {
                    Allocator *allocator = m_client->allocator;
                    aws_mqtt_client_clean_up(m_client);
                    aws_mem_release(allocator, reinterpret_cast<void *>(m_client));
                    m_client = nullptr;
                }
            }

            MqttClient::MqttClient(MqttClient &&toMove) noexcept : m_client(toMove.m_client)
            {
                toMove.m_client = nullptr;
            }

            MqttClient &MqttClient::operator=(MqttClient &&toMove) noexcept
            {
                if (&toMove != this)
                {
                    m_client = toMove.m_client;
                    toMove.m_client = nullptr;
                }

                return *this;
            }

            MqttClient::operator bool() const noexcept { return m_client != nullptr; }

            int MqttClient::LastError() const noexcept { return aws_last_error(); }

            std::shared_ptr<MqttConnection> MqttClient::NewConnection(
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                const Crt::Io::TlsContext &tlsContext,
                bool useWebsocket) noexcept
            {
                // If you're reading this and asking.... why is this so complicated? Why not use make_shared
                // or allocate_shared? Well, MqttConnection constructors are private and stl is dumb like that.
                // so, we do it manually.
                Allocator *allocator = m_client->allocator;
                MqttConnection *toSeat =
                    reinterpret_cast<MqttConnection *>(aws_mem_acquire(allocator, sizeof(MqttConnection)));
                if (!toSeat)
                {
                    return nullptr;
                }

                toSeat = new (toSeat) MqttConnection(m_client, hostName, port, socketOptions, tlsContext, useWebsocket);
                return std::shared_ptr<MqttConnection>(toSeat, [allocator](MqttConnection *connection) {
                    connection->~MqttConnection();
                    aws_mem_release(allocator, reinterpret_cast<void *>(connection));
                });
            }

            std::shared_ptr<MqttConnection> MqttClient::NewConnection(
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                bool useWebsocket) noexcept

            {
                // If you're reading this and asking.... why is this so complicated? Why not use make_shared
                // or allocate_shared? Well, MqttConnection constructors are private and stl is dumb like that.
                // so, we do it manually.
                Allocator *allocator = m_client->allocator;
                MqttConnection *toSeat =
                    reinterpret_cast<MqttConnection *>(aws_mem_acquire(m_client->allocator, sizeof(MqttConnection)));
                if (!toSeat)
                {
                    return nullptr;
                }

                toSeat = new (toSeat) MqttConnection(m_client, hostName, port, socketOptions, useWebsocket);
                return std::shared_ptr<MqttConnection>(toSeat, [allocator](MqttConnection *connection) {
                    connection->~MqttConnection();
                    aws_mem_release(allocator, reinterpret_cast<void *>(connection));
                });
            }
        } // namespace Mqtt
    }     // namespace Crt
} // namespace Aws
