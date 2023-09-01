/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/mqtt/MqttConnection.h>

#include <aws/crt/mqtt/private/MqttConnectionCore.h>

#define AWS_MQTT_MAX_TOPIC_LENGTH 65535

namespace Aws
{
    namespace Crt
    {
        namespace Mqtt
        {
            MqttConnection::~MqttConnection()
            {
                // Request the internal core to release the underlying connection.
                m_connectionCore->Destroy();
            }

            std::shared_ptr<MqttConnection> MqttConnection::s_Create(
                aws_mqtt_client *client,
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                Crt::Io::TlsContext &&tlsContext,
                bool useWebsocket,
                Allocator *allocator)
            {
                if (!tlsContext)
                {
                    AWS_LOGF_ERROR(
                        AWS_LS_MQTT_CLIENT,
                        "id=%p Trying to call MqttClient::NewConnection using an invalid TlsContext.",
                        (void *)client);
                    aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
                    return nullptr;
                }

                auto tlsConnectionOptions = tlsContext.NewConnectionOptions();
                auto connection = MakeShared<MqttConnection>(allocator);
                connection->m_connectionCore = MqttConnectionCore::s_Create(
                    MqttConnectionCore::ConstructionKey{},
                    connection,
                    client,
                    hostName,
                    port,
                    socketOptions,
                    std::move(tlsContext),
                    std::move(tlsConnectionOptions),
                    /*useTls=*/true,
                    useWebsocket,
                    allocator);
                return connection;
            }

            std::shared_ptr<MqttConnection> MqttConnection::s_Create(
                aws_mqtt_client *client,
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                bool useWebsocket,
                Allocator *allocator)
            {
                auto connection = MakeShared<MqttConnection>(allocator);
                Crt::Io::TlsContext tlsContext;
                connection->m_connectionCore = MqttConnectionCore::s_Create(
                    MqttConnectionCore::ConstructionKey{},
                    connection,
                    client,
                    hostName,
                    port,
                    socketOptions,
                    Crt::Io::TlsContext{},
                    Crt::Io::TlsConnectionOptions{},
                    /*useTls=*/false,
                    useWebsocket,
                    allocator);
                return connection;
            }

            std::shared_ptr<MqttConnection> MqttConnection::s_Create(
                aws_mqtt5_client *mqtt5Client,
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                Crt::Io::TlsConnectionOptions &&tlsConnectionOptions,
                bool useWebsocket,
                Allocator *allocator)
            {
                auto connection = MakeShared<MqttConnection>(allocator);
                connection->m_connectionCore = MqttConnectionCore::s_Create(
                    MqttConnectionCore::ConstructionKey{},
                    connection,
                    mqtt5Client,
                    hostName,
                    port,
                    socketOptions,
                    Crt::Io::TlsContext{},
                    std::move(tlsConnectionOptions),
                    /*useTls=*/true,
                    useWebsocket,
                    allocator);
                return connection;
            }

            std::shared_ptr<MqttConnection> MqttConnection::s_Create(
                aws_mqtt5_client *mqtt5Client,
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                bool useWebsocket,
                Allocator *allocator)
            {
                auto connection = MakeShared<MqttConnection>(allocator);
                connection->m_connectionCore = MqttConnectionCore::s_Create(
                    MqttConnectionCore::ConstructionKey{},
                    connection,
                    mqtt5Client,
                    hostName,
                    port,
                    socketOptions,
                    Crt::Io::TlsContext{},
                    Crt::Io::TlsConnectionOptions{},
                    /*useTls=*/true,
                    useWebsocket,
                    allocator);
                return connection;
            }

            MqttConnection::operator bool() const noexcept { return m_connectionCore->operator bool(); }

            int MqttConnection::LastError() const noexcept { return m_connectionCore->LastError(); }

            bool MqttConnection::SetWill(const char *topic, QOS qos, bool retain, const ByteBuf &payload) noexcept
            {
                return m_connectionCore->SetWill(topic, qos, retain, payload);
            }

            bool MqttConnection::SetLogin(const char *username, const char *password) noexcept
            {
                return m_connectionCore->SetLogin(username, password);
            }

            bool MqttConnection::SetWebsocketProxyOptions(
                const Http::HttpClientConnectionProxyOptions &proxyOptions) noexcept
            {
                return m_connectionCore->SetHttpProxyOptions(proxyOptions);
            }

            bool MqttConnection::SetHttpProxyOptions(
                const Http::HttpClientConnectionProxyOptions &proxyOptions) noexcept
            {
                return m_connectionCore->SetHttpProxyOptions(proxyOptions);
            }

            bool MqttConnection::SetReconnectTimeout(uint64_t min_seconds, uint64_t max_seconds) noexcept
            {
                return m_connectionCore->SetReconnectTimeout(min_seconds, max_seconds);
            }

            bool MqttConnection::Connect(
                const char *clientId,
                bool cleanSession,
                uint16_t keepAliveTime,
                uint32_t pingTimeoutMs,
                uint32_t protocolOperationTimeoutMs) noexcept
            {
                bool setWebSocketInterceptor = static_cast<bool>(WebsocketInterceptor);
                return m_connectionCore->Connect(
                    clientId,
                    cleanSession,
                    keepAliveTime,
                    pingTimeoutMs,
                    protocolOperationTimeoutMs,
                    setWebSocketInterceptor);
            }

            bool MqttConnection::Disconnect() noexcept { return m_connectionCore->Disconnect(); }

            aws_mqtt_client_connection *MqttConnection::GetUnderlyingConnection() noexcept
            {
                return m_connectionCore->GetUnderlyingConnection();
            }

            bool MqttConnection::SetOnMessageHandler(OnPublishReceivedHandler &&onPublish) noexcept
            {
                return m_connectionCore->SetOnMessageHandler(
                    [onPublish](
                        MqttConnection &connection, const String &topic, const ByteBuf &payload, bool, QOS, bool) {
                        onPublish(connection, topic, payload);
                    });
            }

            bool MqttConnection::SetOnMessageHandler(OnMessageReceivedHandler &&onMessage) noexcept
            {
                return m_connectionCore->SetOnMessageHandler(std::move(onMessage));
            }

            uint16_t MqttConnection::Subscribe(
                const char *topicFilter,
                QOS qos,
                OnPublishReceivedHandler &&onPublish,
                OnSubAckHandler &&onSubAck) noexcept
            {
                return m_connectionCore->Subscribe(
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
                return m_connectionCore->Subscribe(topicFilter, qos, std::move(onMessage), std::move(onSubAck));
            }

            uint16_t MqttConnection::Subscribe(
                const Vector<std::pair<const char *, OnPublishReceivedHandler>> &topicFilters,
                QOS qos,
                OnMultiSubAckHandler &&onOpComplete) noexcept
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
                return m_connectionCore->Subscribe(newTopicFilters, qos, std::move(onOpComplete));
            }

            uint16_t MqttConnection::Subscribe(
                const Vector<std::pair<const char *, OnMessageReceivedHandler>> &topicFilters,
                QOS qos,
                OnMultiSubAckHandler &&onOpComplete) noexcept
            {
                return m_connectionCore->Subscribe(topicFilters, qos, std::move(onOpComplete));
            }

            uint16_t MqttConnection::Unsubscribe(
                const char *topicFilter,
                OnOperationCompleteHandler &&onOpComplete) noexcept
            {
                return m_connectionCore->Unsubscribe(topicFilter, std::move(onOpComplete));
            }

            uint16_t MqttConnection::Publish(
                const char *topic,
                QOS qos,
                bool retain,
                const ByteBuf &payload,
                OnOperationCompleteHandler &&onOpComplete) noexcept
            {
                return m_connectionCore->Publish(topic, qos, retain, payload, std::move(onOpComplete));
            }

            const MqttConnectionOperationStatistics &MqttConnection::GetOperationStatistics() noexcept
            {
                return m_connectionCore->GetOperationStatistics();
            }
        } // namespace Mqtt
    }     // namespace Crt
} // namespace Aws
