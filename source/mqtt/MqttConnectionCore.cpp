/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/mqtt/MqttConnection.h>
#include <aws/crt/mqtt/MqttConnectionCore.h>

/* #include <aws/crt/Api.h> */
/* #include <aws/crt/StlAllocator.h> */
/* #include <aws/crt/http/HttpProxyStrategy.h> */
#include <aws/crt/http/HttpRequestResponse.h>
/* #include <aws/crt/io/Bootstrap.h> */

#include <utility>

#define AWS_MQTT_MAX_TOPIC_LENGTH 65535

namespace Aws
{
    namespace Crt
    {
        namespace Mqtt
        {
            void MqttConnectionCore::s_onConnectionInterrupted(
                aws_mqtt_client_connection * /*connection*/,
                int errorCode,
                void *userData)
            {
                auto *connectionCore = reinterpret_cast<MqttConnectionCore *>(userData);

                std::shared_ptr<MqttConnection> connection;
                {
                    std::lock_guard<std::mutex> lock(connectionCore->m_connectionMutex);
                    // Connection is not accessible anymore.
                    if (!connectionCore->m_isConnectionAlive)
                    {
                        return;
                    }
                    connection = connectionCore->m_connection.lock();
                }
                if (!connection)
                {
                    return;
                }

                // At this point we ensured that the MqttConnection object will be alive for the duration of the
                // callback execution, so no critical section is needed.

                if (connection->OnConnectionInterrupted)
                {
                    connection->OnConnectionInterrupted(*connection, errorCode);
                }
            }

            void MqttConnectionCore::s_onConnectionResumed(
                aws_mqtt_client_connection * /*connection*/,
                ReturnCode returnCode,
                bool sessionPresent,
                void *userData)
            {
                auto *connectionCore = reinterpret_cast<MqttConnectionCore *>(userData);

                std::shared_ptr<MqttConnection> connection;
                {
                    std::lock_guard<std::mutex> lock(connectionCore->m_connectionMutex);
                    // Connection is not accessible anymore.
                    if (!connectionCore->m_isConnectionAlive)
                    {
                        return;
                    }
                    connection = connectionCore->m_connection.lock();
                }
                if (!connection)
                {
                    return;
                }

                // At this point we ensured that the MqttConnection object will be alive for the duration of the
                // callback execution, so no critical section is needed.

                if (connection->OnConnectionResumed)
                {
                    connection->OnConnectionResumed(*connection, returnCode, sessionPresent);
                }
                if (connection->OnConnectionSuccess)
                {
                    OnConnectionSuccessData callbackData;
                    callbackData.returnCode = returnCode;
                    callbackData.sessionPresent = sessionPresent;
                    connection->OnConnectionSuccess(*connection, &callbackData);
                }
            }

            void MqttConnectionCore::s_onConnectionClosed(
                aws_mqtt_client_connection * /*underlying_connection*/,
                on_connection_closed_data *data,
                void *userData)
            {
                (void)data;

                auto *connectionCore = reinterpret_cast<MqttConnectionCore *>(userData);

                std::shared_ptr<MqttConnection> connection;
                {
                    std::lock_guard<std::mutex> lock(connectionCore->m_connectionMutex);
                    // Connection is not accessible anymore.
                    if (!connectionCore->m_isConnectionAlive)
                    {
                        return;
                    }
                    connection = connectionCore->m_connection.lock();
                }
                if (!connection)
                {
                    return;
                }

                // At this point we ensured that the MqttConnection object will be alive for the duration of the
                // callback execution, so no critical section is needed.

                if (connection->OnConnectionClosed)
                {
                    connection->OnConnectionClosed(*connection, nullptr);
                }
            }

            void MqttConnectionCore::s_onConnectionCompleted(
                aws_mqtt_client_connection * /*underlying_connection*/,
                int errorCode,
                enum aws_mqtt_connect_return_code returnCode,
                bool sessionPresent,
                void *userData)
            {
                auto *connectionCore = reinterpret_cast<MqttConnectionCore *>(userData);

                std::shared_ptr<MqttConnection> connection;
                {
                    std::lock_guard<std::mutex> lock(connectionCore->m_connectionMutex);
                    // Connection is not accessible anymore.
                    if (!connectionCore->m_isConnectionAlive)
                    {
                        return;
                    }
                    connection = connectionCore->m_connection.lock();
                }
                if (!connection)
                {
                    return;
                }

                // At this point we ensured that the MqttConnection object will be alive for the duration of the
                // callback execution, so no critical section is needed.

                if (connection->OnConnectionCompleted)
                {
                    connection->OnConnectionCompleted(*connection, errorCode, returnCode, sessionPresent);
                }
            }

            void MqttConnectionCore::s_onConnectionSuccess(
                aws_mqtt_client_connection * /*underlying_connection*/,
                ReturnCode returnCode,
                bool sessionPresent,
                void *userData)
            {
                auto *connectionCore = reinterpret_cast<MqttConnectionCore *>(userData);

                std::shared_ptr<MqttConnection> connection;
                {
                    std::lock_guard<std::mutex> lock(connectionCore->m_connectionMutex);
                    // Connection is not accessible anymore.
                    if (!connectionCore->m_isConnectionAlive)
                    {
                        return;
                    }
                    connection = connectionCore->m_connection.lock();
                }
                if (!connection)
                {
                    return;
                }

                // At this point we ensured that the MqttConnection object will be alive for the duration of the
                // callback execution, so no critical section is needed.

                if (connection->OnConnectionSuccess)
                {
                    OnConnectionSuccessData callbackData;
                    callbackData.returnCode = returnCode;
                    callbackData.sessionPresent = sessionPresent;
                    connection->OnConnectionSuccess(*connection, &callbackData);
                }
            }

            void MqttConnectionCore::s_onConnectionFailure(
                aws_mqtt_client_connection * /*underlying_connection*/,
                int errorCode,
                void *userData)
            {
                auto *connectionCore = reinterpret_cast<MqttConnectionCore *>(userData);

                std::shared_ptr<MqttConnection> connection;
                {
                    std::lock_guard<std::mutex> lock(connectionCore->m_connectionMutex);
                    // Connection is not accessible anymore.
                    if (!connectionCore->m_isConnectionAlive)
                    {
                        return;
                    }
                    connection = connectionCore->m_connection.lock();
                }
                if (!connection)
                {
                    return;
                }

                // At this point we ensured that the MqttConnection object will be alive for the duration of the
                // callback execution, so no critical section is needed.

                if (connection->OnConnectionFailure)
                {
                    OnConnectionFailureData callbackData;
                    callbackData.error = errorCode;
                    connection->OnConnectionFailure(*connection, &callbackData);
                }
            }

            void MqttConnectionCore::s_onDisconnect(
                aws_mqtt_client_connection * /*underlying_connection*/,
                void *userData)
            {
                auto *connectionCore = reinterpret_cast<MqttConnectionCore *>(userData);

                std::shared_ptr<MqttConnection> connection;
                {
                    std::lock_guard<std::mutex> lock(connectionCore->m_connectionMutex);
                    // Connection is not accessible anymore.
                    if (!connectionCore->m_isConnectionAlive)
                    {
                        return;
                    }
                    connection = connectionCore->m_connection.lock();
                }
                if (!connection)
                {
                    return;
                }

                // At this point we ensured that the MqttConnection object will be alive for the duration of the
                // callback execution, so no critical section is needed.

                if (connection->OnDisconnect)
                {
                    connection->OnDisconnect(*connection);
                }
            }

            struct PubCallbackData
            {
                MqttConnection *connection = nullptr;
                OnMessageReceivedHandler onMessageReceived;
                Allocator *allocator = nullptr;
            };

            static void s_cleanUpOnPublishData(void *userData)
            {
                auto *callbackData = reinterpret_cast<PubCallbackData *>(userData);
                Crt::Delete(callbackData, callbackData->allocator);
            }

            void MqttConnectionCore::s_onPublish(
                aws_mqtt_client_connection * /*underlyingConnection*/,
                const aws_byte_cursor *topic,
                const aws_byte_cursor *payload,
                bool dup,
                enum aws_mqtt_qos qos,
                bool retain,
                void *userData)
            {
                auto *callbackData = reinterpret_cast<PubCallbackData *>(userData);

                if (callbackData->onMessageReceived)
                {
                    String topicStr(reinterpret_cast<char *>(topic->ptr), topic->len);
                    ByteBuf payloadBuf = aws_byte_buf_from_array(payload->ptr, payload->len);
                    callbackData->onMessageReceived(
                        *(callbackData->connection), topicStr, payloadBuf, dup, qos, retain);
                }
            }

            struct OpCompleteCallbackData
            {
                MqttConnection *connection = nullptr;
                OnOperationCompleteHandler onOperationComplete;
                const char *topic = nullptr;
                Allocator *allocator = nullptr;
            };

            void MqttConnectionCore::s_onOpComplete(
                aws_mqtt_client_connection * /*connection*/,
                uint16_t packetId,
                int errorCode,
                void *userData)
            {
                auto *callbackData = reinterpret_cast<OpCompleteCallbackData *>(userData);
                if (callbackData->onOperationComplete)
                {
                    callbackData->onOperationComplete(*callbackData->connection, packetId, errorCode);
                }

                if (callbackData->topic != nullptr)
                {
                    aws_mem_release(
                        callbackData->allocator, reinterpret_cast<void *>(const_cast<char *>(callbackData->topic)));
                }

                Crt::Delete(callbackData, callbackData->allocator);
            }

            struct SubAckCallbackData
            {
                MqttConnectionCore *connectionCore = nullptr;
                OnSubAckHandler onSubAck;
                const char *topic = nullptr;
                Allocator *allocator = nullptr;
            };

            void MqttConnectionCore::s_onSubAck(
                aws_mqtt_client_connection * /*connection*/,
                uint16_t packetId,
                const struct aws_byte_cursor *topic,
                enum aws_mqtt_qos qos,
                int errorCode,
                void *userData)
            {
                auto *callbackData = reinterpret_cast<SubAckCallbackData *>(userData);

                if (callbackData->onSubAck)
                {
                    String topicStr(reinterpret_cast<char *>(topic->ptr), topic->len);
                    callbackData->onSubAck(*callbackData->connection, packetId, topicStr, qos, errorCode);
                }

                if (callbackData->topic != nullptr)
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

            void MqttConnectionCore::s_onMultiSubAck(
                aws_mqtt_client_connection * /*connection*/,
                uint16_t packetId,
                const struct aws_array_list *topicSubacks,
                int errorCode,
                void *userData)
            {
                auto *callbackData = reinterpret_cast<MultiSubAckCallbackData *>(userData);

                if (callbackData->onSubAck)
                {
                    size_t length = aws_array_list_length(topicSubacks);
                    Vector<String> topics;
                    topics.reserve(length);
                    QOS qos = AWS_MQTT_QOS_AT_MOST_ONCE;
                    for (size_t i = 0; i < length; ++i)
                    {
                        aws_mqtt_topic_subscription *subscription = nullptr;
                        aws_array_list_get_at(topicSubacks, &subscription, i);
                        topics.push_back(
                            String(reinterpret_cast<char *>(subscription->topic.ptr), subscription->topic.len));
                        qos = subscription->qos;
                    }

                    callbackData->onSubAck(*callbackData->connection, packetId, topics, qos, errorCode);
                }

                if (callbackData->topic != nullptr)
                {
                    aws_mem_release(
                        callbackData->allocator, reinterpret_cast<void *>(const_cast<char *>(callbackData->topic)));
                }

                Crt::Delete(callbackData, callbackData->allocator);
            }

            void MqttConnectionCore::s_connectionInit(
                MqttConnectionCore *self,
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                aws_mqtt5_client *mqtt5Client)
            {
                self->m_hostName = String(hostName);
                self->m_port = port;
                self->m_socketOptions = socketOptions;

                if (mqtt5Client != nullptr)
                {
                    self->m_underlyingConnection = aws_mqtt_client_connection_new_from_mqtt5_client(mqtt5Client);
                }
                else
                {
                    self->m_underlyingConnection = aws_mqtt_client_connection_new(self->m_owningClient);
                }

                if (self->m_underlyingConnection != nullptr)
                {
                    aws_mqtt_client_connection_set_connection_result_handlers(
                        self->m_underlyingConnection,
                        MqttConnectionCore::s_onConnectionSuccess,
                        self,
                        MqttConnectionCore::s_onConnectionFailure,
                        self);

                    aws_mqtt_client_connection_set_connection_interruption_handlers(
                        self->m_underlyingConnection,
                        MqttConnectionCore::s_onConnectionInterrupted,
                        self,
                        MqttConnectionCore::s_onConnectionResumed,
                        self);

                    aws_mqtt_client_connection_set_connection_closed_handler(
                        self->m_underlyingConnection, MqttConnectionCore::s_onConnectionClosed, self);
                }
                else
                {
                    AWS_LOGF_DEBUG(AWS_LS_MQTT_CLIENT, "Failed to initialize Mqtt Connection");
                }
            }

            void MqttConnectionCore::s_onWebsocketHandshake(
                struct aws_http_message *rawRequest,
                void *user_data,
                aws_mqtt_transform_websocket_handshake_complete_fn *complete_fn,
                void *complete_ctx)
            {
                auto *connection = reinterpret_cast<MqttConnectionCore *>(user_data);

                Allocator *allocator = connection->m_allocator;
                // we have to do this because of private constructors.
                auto *toSeat =
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

            MqttConnectionCore::MqttConnectionCore(
                aws_mqtt_client *client,
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                const Crt::Io::TlsContext &tlsContext,
                bool useWebsocket) noexcept
                : m_owningClient(client), m_tlsContext(tlsContext), m_tlsOptions(tlsContext.NewConnectionOptions()),
                  m_onAnyCbData(nullptr), m_useTls(true), m_useWebsocket(useWebsocket), m_allocator(client->allocator)
            {
                s_connectionInit(this, hostName, port, socketOptions);
            }

            MqttConnectionCore::MqttConnectionCore(
                aws_mqtt_client *client,
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                bool useWebsocket) noexcept
                : m_owningClient(client), m_onAnyCbData(nullptr), m_useTls(false), m_useWebsocket(useWebsocket),
                  m_allocator(client->allocator)
            {
                s_connectionInit(this, hostName, port, socketOptions);
            }

            MqttConnectionCore::MqttConnectionCore(
                aws_mqtt5_client *mqtt5Client,
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                const Crt::Io::TlsConnectionOptions &tlsConnectionOptions,
                bool useWebsocket,
                Allocator *allocator) noexcept
                : m_owningClient(nullptr), m_tlsOptions(tlsConnectionOptions), m_onAnyCbData(nullptr), m_useTls(true),
                  m_useWebsocket(useWebsocket), m_allocator(allocator)
            {
                s_connectionInit(this, hostName, port, socketOptions, mqtt5Client);
            }

            MqttConnectionCore::MqttConnectionCore(
                aws_mqtt5_client *mqtt5Client,
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                bool useWebsocket,
                Allocator *allocator) noexcept
                : m_owningClient(nullptr), m_onAnyCbData(nullptr), m_useTls(false), m_useWebsocket(useWebsocket),
                  m_allocator(allocator)
            {
                s_connectionInit(this, hostName, port, socketOptions, mqtt5Client);
            }

            MqttConnectionCore::~MqttConnectionCore()
            {
                if (*this)
                {
                    // Get rid of the on_closed callback, because if we are destroying the connection we do not care.
                    aws_mqtt_client_connection_set_connection_closed_handler(m_underlyingConnection, nullptr, nullptr);

                    aws_mqtt_client_connection_release(m_underlyingConnection);

                    if (m_onAnyCbData != nullptr)
                    {
                        auto *pubCallbackData = reinterpret_cast<PubCallbackData *>(m_onAnyCbData);
                        Crt::Delete(pubCallbackData, pubCallbackData->allocator);
                    }
                }
            }

            MqttConnectionCore::operator bool() const noexcept { return m_underlyingConnection != nullptr; }

            int MqttConnectionCore::LastError() const noexcept { return aws_last_error(); }

            bool MqttConnectionCore::SetWill(const char *topic, QOS qos, bool retain, const ByteBuf &payload) noexcept
            {
                ByteBuf topicBuf = aws_byte_buf_from_c_str(topic);
                ByteCursor topicCur = aws_byte_cursor_from_buf(&topicBuf);
                ByteCursor payloadCur = aws_byte_cursor_from_buf(&payload);

                return aws_mqtt_client_connection_set_will(
                           m_underlyingConnection, &topicCur, qos, retain, &payloadCur) == 0;
            }

            bool MqttConnectionCore::SetLogin(const char *userName, const char *password) noexcept
            {
                ByteBuf userNameBuf = aws_byte_buf_from_c_str(userName);
                ByteCursor userNameCur = aws_byte_cursor_from_buf(&userNameBuf);

                ByteCursor *pwdCurPtr = nullptr;
                ByteCursor pwdCur;

                if (password != nullptr)
                {
                    pwdCur = ByteCursorFromCString(password);
                    pwdCurPtr = &pwdCur;
                }
                return aws_mqtt_client_connection_set_login(m_underlyingConnection, &userNameCur, pwdCurPtr) == 0;
            }

            bool MqttConnectionCore::SetWebsocketProxyOptions(
                const Http::HttpClientConnectionProxyOptions &proxyOptions) noexcept
            {
                m_proxyOptions = proxyOptions;
                return true;
            }

            bool MqttConnectionCore::SetHttpProxyOptions(
                const Http::HttpClientConnectionProxyOptions &proxyOptions) noexcept
            {
                m_proxyOptions = proxyOptions;
                return true;
            }

            bool MqttConnectionCore::SetReconnectTimeout(uint64_t min_seconds, uint64_t max_seconds) noexcept
            {
                return aws_mqtt_client_connection_set_reconnect_timeout(
                           m_underlyingConnection, min_seconds, max_seconds) == 0;
            }

            bool MqttConnectionCore::Connect(
                const char *clientId,
                bool cleanSession,
                uint16_t keepAliveTime,
                uint32_t pingTimeoutMs,
                uint32_t protocolOperationTimeoutMs) noexcept
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
                options.ping_timeout_ms = pingTimeoutMs;
                options.protocol_operation_timeout_ms = protocolOperationTimeoutMs;
                options.on_connection_complete = MqttConnectionCore::s_onConnectionCompleted;
                options.user_data = this;

                if (m_useWebsocket)
                {
                    if (WebsocketInterceptor)
                    {
                        if (aws_mqtt_client_connection_use_websockets(
                                m_underlyingConnection,
                                MqttConnectionCore::s_onWebsocketHandshake,
                                this,
                                nullptr,
                                nullptr))
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
                }

                if (m_proxyOptions)
                {
                    struct aws_http_proxy_options proxyOptions;
                    m_proxyOptions->InitializeRawProxyOptions(proxyOptions);

                    if (aws_mqtt_client_connection_set_http_proxy_options(m_underlyingConnection, &proxyOptions) != 0)
                    {
                        return false;
                    }
                }

                return aws_mqtt_client_connection_connect(m_underlyingConnection, &options) == AWS_OP_SUCCESS;
            }

            bool MqttConnectionCore::Disconnect() noexcept
            {
                return aws_mqtt_client_connection_disconnect(
                           m_underlyingConnection, MqttConnectionCore::s_onDisconnect, this) == AWS_OP_SUCCESS;
            }

            aws_mqtt_client_connection *MqttConnectionCore::GetUnderlyingConnection() noexcept
            {
                return m_underlyingConnection;
            }

            bool MqttConnectionCore::SetOnMessageHandler(OnPublishReceivedHandler &&onPublish) noexcept
            {
                return SetOnMessageHandler(
                    [onPublish](
                        MqttConnectionCore &connection, const String &topic, const ByteBuf &payload, bool, QOS, bool) {
                        onPublish(connection, topic, payload);
                    });
            }

            bool MqttConnectionCore::SetOnMessageHandler(OnMessageReceivedHandler &&onMessage) noexcept
            {
                auto *pubCallbackData = Aws::Crt::New<PubCallbackData>(m_allocator);
                if (pubCallbackData == nullptr)
                {
                    return false;
                }

                pubCallbackData->connection = this;
                pubCallbackData->onMessageReceived = std::move(onMessage);
                pubCallbackData->allocator = m_allocator;

                if (aws_mqtt_client_connection_set_on_any_publish_handler(
                        m_underlyingConnection, s_onPublish, pubCallbackData) == 0)
                {
                    m_onAnyCbData = reinterpret_cast<void *>(pubCallbackData);
                    return true;
                }

                Aws::Crt::Delete(pubCallbackData, pubCallbackData->allocator);
                return false;
            }

            uint16_t MqttConnectionCore::Subscribe(
                const char *topicFilter,
                QOS qos,
                OnPublishReceivedHandler &&onPublish,
                OnSubAckHandler &&onSubAck) noexcept
            {
                return Subscribe(
                    topicFilter,
                    qos,
                    [onPublish](
                        MqttConnectionCore &connection, const String &topic, const ByteBuf &payload, bool, QOS, bool) {
                        onPublish(connection, topic, payload);
                    },
                    std::move(onSubAck));
            }

            uint16_t MqttConnectionCore::Subscribe(
                const char *topicFilter,
                QOS qos,
                OnMessageReceivedHandler &&onMessage,
                OnSubAckHandler &&onSubAck) noexcept
            {
                auto *pubCallbackData = Crt::New<PubCallbackData>(m_allocator);

                if (pubCallbackData == nullptr)
                {
                    return 0;
                }

                pubCallbackData->connection = this;
                pubCallbackData->onMessageReceived = std::move(onMessage);
                pubCallbackData->allocator = m_allocator;

                auto *subAckCallbackData = Crt::New<SubAckCallbackData>(m_allocator);

                if (subAckCallbackData == nullptr)
                {
                    Crt::Delete(pubCallbackData, m_allocator);
                    return 0;
                }

                subAckCallbackData->connectionCore = this;
                subAckCallbackData->allocator = m_allocator;
                subAckCallbackData->onSubAck = std::move(onSubAck);
                subAckCallbackData->topic = nullptr;
                subAckCallbackData->allocator = m_allocator;

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

                if (packetId == 0U)
                {
                    Crt::Delete(pubCallbackData, pubCallbackData->allocator);
                    Crt::Delete(subAckCallbackData, subAckCallbackData->allocator);
                }

                return packetId;
            }

            uint16_t MqttConnectionCore::Subscribe(
                const Vector<std::pair<const char *, OnPublishReceivedHandler>> &topicFilters,
                QOS qos,
                OnMultiSubAckHandler &&onSubAck) noexcept
            {
                Vector<std::pair<const char *, OnMessageReceivedHandler>> newTopicFilters;
                newTopicFilters.reserve(topicFilters.size());
                for (const auto &pair : topicFilters)
                {
                    const OnPublishReceivedHandler &pubHandler = pair.second;
                    newTopicFilters.emplace_back(
                        pair.first,
                        [pubHandler](
                            MqttConnectionCore &connection,
                            const String &topic,
                            const ByteBuf &payload,
                            bool,
                            QOS,
                            bool) { pubHandler(connection, topic, payload); });
                }
                return Subscribe(newTopicFilters, qos, std::move(onSubAck));
            }

            uint16_t MqttConnectionCore::Subscribe(
                const Vector<std::pair<const char *, OnMessageReceivedHandler>> &topicFilters,
                QOS qos,
                OnMultiSubAckHandler &&onSubAck) noexcept
            {
                uint16_t packetId = 0;
                auto *subAckCallbackData = Crt::New<MultiSubAckCallbackData>(m_allocator);

                if (subAckCallbackData == nullptr)
                {
                    return 0;
                }

                aws_array_list multiPub;
                AWS_ZERO_STRUCT(multiPub);

                if (aws_array_list_init_dynamic(
                        &multiPub, m_allocator, topicFilters.size(), sizeof(aws_mqtt_topic_subscription)) != 0)
                {
                    Crt::Delete(subAckCallbackData, m_allocator);
                    return 0;
                }

                for (const auto &topicFilter : topicFilters)
                {
                    auto *pubCallbackData = Crt::New<PubCallbackData>(m_allocator);

                    if (pubCallbackData == nullptr)
                    {
                        goto clean_up;
                    }

                    pubCallbackData->connection = this;
                    pubCallbackData->onMessageReceived = topicFilter.second;
                    pubCallbackData->allocator = m_allocator;

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
                subAckCallbackData->allocator = m_allocator;
                subAckCallbackData->onSubAck = std::move(onSubAck);
                subAckCallbackData->topic = nullptr;
                subAckCallbackData->allocator = m_allocator;

                packetId = aws_mqtt_client_connection_subscribe_multiple(
                    m_underlyingConnection, &multiPub, s_onMultiSubAck, subAckCallbackData);

            clean_up:
                if (packetId == 0U)
                {
                    size_t length = aws_array_list_length(&multiPub);
                    for (size_t i = 0; i < length; ++i)
                    {
                        aws_mqtt_topic_subscription *subscription = nullptr;
                        aws_array_list_get_at_ptr(&multiPub, reinterpret_cast<void **>(&subscription), i);
                        auto *pubCallbackData = reinterpret_cast<PubCallbackData *>(subscription->on_publish_ud);
                        Crt::Delete(pubCallbackData, m_allocator);
                    }

                    Crt::Delete(subAckCallbackData, m_allocator);
                }

                aws_array_list_clean_up(&multiPub);

                return packetId;
            }

            uint16_t MqttConnectionCore::Unsubscribe(
                const char *topicFilter,
                OnOperationCompleteHandler &&onOpComplete) noexcept
            {
                auto *opCompleteCallbackData = Crt::New<OpCompleteCallbackData>(m_allocator);

                if (opCompleteCallbackData == nullptr)
                {
                    return 0;
                }

                opCompleteCallbackData->connection = this;
                opCompleteCallbackData->allocator = m_allocator;
                opCompleteCallbackData->onOperationComplete = std::move(onOpComplete);
                opCompleteCallbackData->topic = nullptr;
                ByteBuf topicFilterBuf = aws_byte_buf_from_c_str(topicFilter);
                ByteCursor topicFilterCur = aws_byte_cursor_from_buf(&topicFilterBuf);

                uint16_t packetId = aws_mqtt_client_connection_unsubscribe(
                    m_underlyingConnection, &topicFilterCur, s_onOpComplete, opCompleteCallbackData);

                if (packetId == 0U)
                {
                    Crt::Delete(opCompleteCallbackData, m_allocator);
                }

                return packetId;
            }

            uint16_t MqttConnectionCore::Publish(
                const char *topic,
                QOS qos,
                bool retain,
                const ByteBuf &payload,
                OnOperationCompleteHandler &&onOpComplete) noexcept
            {

                auto *opCompleteCallbackData = Crt::New<OpCompleteCallbackData>(m_allocator);
                if (opCompleteCallbackData == nullptr)
                {
                    return 0;
                }

                size_t topicLen = strnlen(topic, AWS_MQTT_MAX_TOPIC_LENGTH) + 1;
                char *topicCpy = reinterpret_cast<char *>(aws_mem_calloc(m_allocator, topicLen, sizeof(char)));

                if (topicCpy == nullptr)
                {
                    Crt::Delete(opCompleteCallbackData, m_allocator);
                }

                memcpy(topicCpy, topic, topicLen);

                opCompleteCallbackData->connection = this;
                opCompleteCallbackData->allocator = m_allocator;
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

                if (packetId == 0U)
                {
                    aws_mem_release(m_allocator, reinterpret_cast<void *>(topicCpy));
                    Crt::Delete(opCompleteCallbackData, m_allocator);
                }

                return packetId;
            }

            const MqttConnectionOperationStatistics &MqttConnectionCore::GetOperationStatistics() noexcept
            {
                aws_mqtt_connection_operation_statistics m_operationStatisticsNative = {0, 0, 0, 0};
                if (m_underlyingConnection != nullptr)
                {
                    aws_mqtt_client_connection_get_stats(m_underlyingConnection, &m_operationStatisticsNative);
                    m_operationStatistics.incompleteOperationCount =
                        m_operationStatisticsNative.incomplete_operation_count;
                    m_operationStatistics.incompleteOperationSize =
                        m_operationStatisticsNative.incomplete_operation_size;
                    m_operationStatistics.unackedOperationCount = m_operationStatisticsNative.unacked_operation_count;
                    m_operationStatistics.unackedOperationSize = m_operationStatisticsNative.unacked_operation_size;
                }
                return m_operationStatistics;
            }
        } // namespace Mqtt
    }     // namespace Crt
} // namespace Aws
