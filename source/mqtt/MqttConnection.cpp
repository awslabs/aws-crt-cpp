/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/mqtt/MqttConnection.h>

#include <aws/crt/Api.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/http/HttpProxyStrategy.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/Bootstrap.h>

#include <utility>

#define AWS_MQTT_MAX_TOPIC_LENGTH 65535

namespace Aws
{
    namespace Crt
    {
        namespace Mqtt
        {
            MqttConnection::MqttConnection(
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

            MqttConnection::MqttConnection(
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

            MqttConnection::MqttConnection(
                aws_mqtt5_client *mqtt5Client,
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                const Crt::Io::TlsConnectionOptions &tlsOptions,
                bool useWebsocket,
                Allocator *allocator) noexcept
                : m_owningClient(nullptr), m_tlsOptions(tlsOptions), m_onAnyCbData(nullptr), m_useTls(true),
                  m_useWebsocket(useWebsocket), m_allocator(allocator)
            {
                s_connectionInit(this, hostName, port, socketOptions, mqtt5Client);
            }

            MqttConnection::MqttConnection(
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

            MqttConnection::~MqttConnection()
            {
                if (*this)
                {
                    // Get rid of the on_closed callback, because if we are destroying the connection we do not care.
                    aws_mqtt_client_connection_set_connection_closed_handler(m_underlyingConnection, nullptr, nullptr);

                    aws_mqtt_client_connection_release(m_underlyingConnection);

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

            bool MqttConnection::SetHttpProxyOptions(
                const Http::HttpClientConnectionProxyOptions &proxyOptions) noexcept
            {
                m_proxyOptions = proxyOptions;
                return true;
            }

            bool MqttConnection::SetReconnectTimeout(uint64_t min_seconds, uint64_t max_seconds) noexcept
            {
                return aws_mqtt_client_connection_set_reconnect_timeout(
                           m_underlyingConnection, min_seconds, max_seconds) == 0;
            }

            bool MqttConnection::Connect(
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
                }

                if (m_proxyOptions)
                {
                    struct aws_http_proxy_options proxyOptions;
                    m_proxyOptions->InitializeRawProxyOptions(proxyOptions);

                    if (aws_mqtt_client_connection_set_http_proxy_options(m_underlyingConnection, &proxyOptions))
                    {
                        return false;
                    }
                }

                return aws_mqtt_client_connection_connect(m_underlyingConnection, &options) == AWS_OP_SUCCESS;
            }

            bool MqttConnection::Disconnect() noexcept
            {
                return aws_mqtt_client_connection_disconnect(
                           m_underlyingConnection, MqttConnection::s_onDisconnect, this) == AWS_OP_SUCCESS;
            }

            aws_mqtt_client_connection *MqttConnection::GetUnderlyingConnection() noexcept
            {
                return m_underlyingConnection;
            }

            bool MqttConnection::SetOnMessageHandler(OnPublishReceivedHandler &&onPublish) noexcept
            {
                return SetOnMessageHandler(
                    [onPublish](
                        MqttConnection &connection, const String &topic, const ByteBuf &payload, bool, QOS, bool) {
                        onPublish(connection, topic, payload);
                    });
            }

            bool MqttConnection::SetOnMessageHandler(OnMessageReceivedHandler &&onMessage) noexcept
            {
                auto pubCallbackData = Aws::Crt::New<PubCallbackData>(m_allocator);

                if (!pubCallbackData)
                {
                    return false;
                }

                pubCallbackData->connection = this;
                pubCallbackData->onMessageReceived = std::move(onMessage);
                pubCallbackData->allocator = m_allocator;

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
                return Subscribe(
                    topicFilter,
                    qos,
                    [onPublish](
                        MqttConnection &connection, const String &topic, const ByteBuf &payload, bool, QOS, bool) {
                        onPublish(connection, topic, payload);
                    },
                    std::move(onSubAck));
            }

            uint16_t MqttConnection::Subscribe(
                const char *topicFilter,
                QOS qos,
                OnMessageReceivedHandler &&onMessage,
                OnSubAckHandler &&onSubAck) noexcept
            {
                auto pubCallbackData = Crt::New<PubCallbackData>(m_allocator);

                if (!pubCallbackData)
                {
                    return 0;
                }

                pubCallbackData->connection = this;
                pubCallbackData->onMessageReceived = std::move(onMessage);
                pubCallbackData->allocator = m_allocator;

                auto subAckCallbackData = Crt::New<SubAckCallbackData>(m_allocator);

                if (!subAckCallbackData)
                {
                    Crt::Delete(pubCallbackData, m_allocator);
                    return 0;
                }

                subAckCallbackData->connection = this;
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
                Vector<std::pair<const char *, OnMessageReceivedHandler>> newTopicFilters;
                newTopicFilters.reserve(topicFilters.size());
                for (const auto &pair : topicFilters)
                {
                    const OnPublishReceivedHandler &pubHandler = pair.second;
                    newTopicFilters.emplace_back(
                        pair.first,
                        [pubHandler](
                            MqttConnection &connection, const String &topic, const ByteBuf &payload, bool, QOS, bool) {
                            pubHandler(connection, topic, payload);
                        });
                }
                return Subscribe(newTopicFilters, qos, std::move(onSubAck));
            }

            uint16_t MqttConnection::Subscribe(
                const Vector<std::pair<const char *, OnMessageReceivedHandler>> &topicFilters,
                QOS qos,
                OnMultiSubAckHandler &&onSubAck) noexcept
            {
                uint16_t packetId = 0;
                auto subAckCallbackData = Crt::New<MultiSubAckCallbackData>(m_allocator);

                if (!subAckCallbackData)
                {
                    return 0;
                }

                aws_array_list multiPub;
                AWS_ZERO_STRUCT(multiPub);

                if (aws_array_list_init_dynamic(
                        &multiPub, m_allocator, topicFilters.size(), sizeof(aws_mqtt_topic_subscription)))
                {
                    Crt::Delete(subAckCallbackData, m_allocator);
                    return 0;
                }

                for (auto &topicFilter : topicFilters)
                {
                    auto pubCallbackData = Crt::New<PubCallbackData>(m_allocator);

                    if (!pubCallbackData)
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
                if (!packetId)
                {
                    size_t length = aws_array_list_length(&multiPub);
                    for (size_t i = 0; i < length; ++i)
                    {
                        aws_mqtt_topic_subscription *subscription = NULL;
                        aws_array_list_get_at_ptr(&multiPub, reinterpret_cast<void **>(&subscription), i);
                        auto pubCallbackData = reinterpret_cast<PubCallbackData *>(subscription->on_publish_ud);
                        Crt::Delete(pubCallbackData, m_allocator);
                    }

                    Crt::Delete(subAckCallbackData, m_allocator);
                }

                aws_array_list_clean_up(&multiPub);

                return packetId;
            }

            uint16_t MqttConnection::Unsubscribe(
                const char *topicFilter,
                OnOperationCompleteHandler &&onOpComplete) noexcept
            {
                auto opCompleteCallbackData = Crt::New<OpCompleteCallbackData>(m_allocator);

                if (!opCompleteCallbackData)
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

                if (!packetId)
                {
                    Crt::Delete(opCompleteCallbackData, m_allocator);
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

                auto opCompleteCallbackData = Crt::New<OpCompleteCallbackData>(m_allocator);
                if (!opCompleteCallbackData)
                {
                    return 0;
                }

                size_t topicLen = strnlen(topic, AWS_MQTT_MAX_TOPIC_LENGTH) + 1;
                char *topicCpy = reinterpret_cast<char *>(aws_mem_calloc(m_allocator, topicLen, sizeof(char)));

                if (!topicCpy)
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

                if (!packetId)
                {
                    aws_mem_release(m_allocator, reinterpret_cast<void *>(topicCpy));
                    Crt::Delete(opCompleteCallbackData, m_allocator);
                }

                return packetId;
            }

            const MqttConnectionOperationStatistics &MqttConnection::GetOperationStatistics() noexcept
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

            MqttClient::MqttClient(Io::ClientBootstrap &bootstrap, Allocator *allocator) noexcept
                : m_client(aws_mqtt_client_new(allocator, bootstrap.GetUnderlyingHandle()))
            {
            }

            MqttClient::MqttClient(Allocator *allocator) noexcept
                : m_client(aws_mqtt_client_new(
                      allocator,
                      Crt::ApiHandle::GetOrCreateStaticDefaultClientBootstrap()->GetUnderlyingHandle()))
            {
            }

            MqttClient::~MqttClient()
            {
                aws_mqtt_client_release(m_client);
                m_client = nullptr;
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
                if (!tlsContext)
                {
                    AWS_LOGF_ERROR(
                        AWS_LS_MQTT_CLIENT,
                        "id=%p Trying to call MqttClient::NewConnection using an invalid TlsContext.",
                        (void *)m_client);
                    aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
                    return nullptr;
                }

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
