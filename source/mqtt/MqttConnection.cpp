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
                bool useWebsocket,
                Allocator *allocator) noexcept
            {
                m_connectionCore = MqttConnectionCore::s_Create(
                    client, hostName, port, socketOptions, tlsContext, useWebsocket, allocator);
            }

            MqttConnection::MqttConnection(
                aws_mqtt_client *client,
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                bool useWebsocket,
                Allocator *allocator) noexcept
            {
                m_connectionCore =
                    MqttConnectionCore::s_Create(client, hostName, port, socketOptions, useWebsocket, allocator);
            }

            MqttConnection::MqttConnection(
                aws_mqtt5_client *mqtt5Client,
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                const Crt::Io::TlsConnectionOptions &tlsConnectionOptions,
                bool useWebsocket,
                Allocator *allocator) noexcept
            {
                m_connectionCore = MqttConnectionCore::s_Create(
                    mqtt5Client, hostName, port, socketOptions, tlsConnectionOptions, useWebsocket, allocator);
            }

            MqttConnection::MqttConnection(
                aws_mqtt5_client *mqtt5Client,
                const char *hostName,
                uint16_t port,
                const Io::SocketOptions &socketOptions,
                bool useWebsocket,
                Allocator *allocator) noexcept
            {
                m_connectionCore =
                    MqttConnectionCore::s_Create(mqtt5Client, hostName, port, socketOptions, useWebsocket, allocator);
            }

            MqttConnection::~MqttConnection()
            {
                // Request for the underlying connection to close.
                m_connectionCore->Close();
            }

            void MqttConnection::Initialize()
            {
                m_connectionCore->Initialize(shared_from_this());
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
                return m_connectionCore->SetWebsocketProxyOptions(proxyOptions);
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
                return m_connectionCore->Connect(
                    clientId, cleanSession, keepAliveTime, pingTimeoutMs, protocolOperationTimeoutMs);
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
                return m_connectionCore->Subscribe(topicFilter, qos, std::move(onPublish), std::move(onSubAck));
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
                return m_connectionCore->Subscribe(topicFilters, qos, std::move(onOpComplete));
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
