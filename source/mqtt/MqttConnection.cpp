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
                if (m_connectionCore)
                {
                    // Request the internal core to release the underlying connection.
                    m_connectionCore->Destroy();
                }
            }

            std::shared_ptr<MqttConnection> MqttConnection::s_CreateMqttConnection(
                MqttConnectionOptions options) noexcept
            {
                auto *allocator = options.allocator;

                auto *toSeat = reinterpret_cast<MqttConnection *>(aws_mem_acquire(allocator, sizeof(MqttConnection)));
                if (toSeat == nullptr)
                {
                    return nullptr;
                }
                toSeat = new (toSeat) MqttConnection();

                auto connection = std::shared_ptr<MqttConnection>(
                    toSeat, [allocator](MqttConnection *mqttConnection) { Crt::Delete(mqttConnection, allocator); });
                // Creation failed, make sure we release the allocated memory
                if (!connection)
                {
                    Crt::Delete(toSeat, allocator);
                    return {};
                }

                connection->m_connectionCore = MqttConnectionCore::s_createMqttConnectionCore(
                    MqttConnectionCore::ConstructionKey{}, connection, std::move(options));
                if (!connection->m_connectionCore || !*connection)
                {
                    return {};
                }

                return connection;
            }

            MqttConnection::operator bool() const noexcept
            {
                return m_connectionCore->operator bool();
            }

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
