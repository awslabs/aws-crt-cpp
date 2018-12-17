#pragma once
/*
 * Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
 */
#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/io/TlsOptions.h>
#include <aws/mqtt/client.h>

#include <functional>
#include <memory>
#include <atomic>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            class ClientBootstrap;
        }

        namespace Mqtt
        {
            class MqttClient;
            class MqttConnection;

            enum class ConnectionState
            {
                Init,
                Connecting,
                Connected,
                Disconnected,
                Error,
            };

            /**
             * Invoked Upon Connection failure.
             */
            using OnConnectionFailedHandler = std::function<void(MqttConnection& connection, int error)>;

            /**
             * Invoked when a connack message is received.
             */
            using OnConnAckHandler = std::function<void(MqttConnection& connection,
                    ReturnCode returnCode, bool sessionPresent)>;

            /**
             * Invoked when a suback message is received.
             */
            using OnSubAckHandler = std::function<void(MqttConnection& connection, uint16_t packetId,
                const String& topic, QOS qos, int errorCode)>;

            /**
             * Invoked when a suback message for multiple topics is received.
             */
            using OnMultiSubAckHandler = std::function<void(MqttConnection& connection, uint16_t packetId,
                    const Vector<String>& topics, QOS qos, int errorCode)>;

            /**
             * Invoked when a disconnect message has been sent.
             */
            using OnDisconnectHandler = std::function<bool(MqttConnection& connection, int error)>;

            /**
             * Invoked upon receipt of a Publish message on a subscribed topic.
             */
            using OnPublishReceivedHandler = std::function<void(MqttConnection& connection, 
                const String& topic, const ByteBuf& payload)>;

            using OnOperationCompleteHandler = std::function<void(MqttConnection& connection, uint16_t packetId, int errorCode)>;

            /**
             * Represents a persistent Mqtt Connection. The memory is owned by MqttClient.
             * To get a new instance of this class, see MqttClient::NewConnection. Unless
             * specified all function arguments need only to live through the duration of the 
             * function call.
             */
            class AWS_CRT_CPP_API MqttConnection final
            {
                friend class MqttClient;
            public:
                ~MqttConnection();
                MqttConnection(const MqttConnection&) = delete;
                MqttConnection(MqttConnection&&) = delete;
                MqttConnection& operator =(const MqttConnection&) = delete;
                MqttConnection& operator =(MqttConnection&&) = delete;

                operator bool() const noexcept;
                int LastError() const noexcept;
                inline ConnectionState GetConnectionState() const noexcept { return m_connectionState; }

                /**
                 * Sets LastWill for the connection. The memory backing payload must outlive the connection.
                 */
                bool SetWill(const char* topic, QOS qos, bool retain,
                        const ByteBuf& payload) noexcept;

                /**
                 * Sets login credentials for the connection. The must get set before the Connect call
                 * if it is to be used. 
                 */
                bool SetLogin(const char* userName, const char* password) noexcept;

                /**
                 * Initiates the connection, OnConnectionFailedHandler and/or OnConnAckHandler will
                 * be invoked in an event-loop thread.
                 */
                bool Connect(const char* clientId, bool cleanSession, uint16_t keepAliveTime) noexcept;

                /**
                 * Initiates disconnect, OnDisconnectHandler will be invoked in an event-loop thread.
                 */
                bool Disconnect() noexcept;

                /**
                 * Subcribes to topicFilter. OnPublishRecievedHandler will be invoked from an event-loop
                 * thread upon an incoming Publish message. OnSubAckHandler will be invoked
                 * upon receipt of a suback message.
                 */
                uint16_t Subscribe(const char* topicFilter, QOS qos,
                        OnPublishReceivedHandler&& onPublish,
                        OnSubAckHandler&& onSubAck) noexcept;

                /**
                 * Subcribes to multiple topicFilters. OnPublishRecievedHandler will be invoked from an event-loop
                 * thread upon an incoming Publish message. OnMultiSubAckHandler will be invoked
                 * upon receipt of a suback message.
                 */
                uint16_t Subscribe(const Vector<std::pair<const char*, OnPublishReceivedHandler>> topicFilters, QOS qos,
                    OnMultiSubAckHandler&& onOpComplete) noexcept;

                /**
                 * Unsubscribes from topicFilter. OnOperationCompleteHandler will be invoked upon receipt of
                 * an unsuback message.
                 */
                uint16_t Unsubscribe(const char* topicFilter,
                        OnOperationCompleteHandler&& onOpComplete) noexcept;

                /**
                 * Publishes to topic. The backing memory for payload must stay available until the 
                 * OnOperationCompleteHandler has been invoked.
                 */
                uint16_t Publish(const char* topic, QOS qos, bool retain, const ByteBuf& payload,
                                 OnOperationCompleteHandler&& onOpComplete) noexcept;

                /**
                 * Sends a ping message.
                 */
                void Ping();

                OnConnectionFailedHandler OnConnectionFailed;
                OnConnAckHandler OnConnAck;
                OnDisconnectHandler OnDisconnect;

            private:
                MqttConnection(aws_mqtt_client* client, const char* hostName, uint16_t port,
                               const Io::SocketOptions& socketOptions,
                               const Io::TlsConnectionOptions& tlsConnOptions) noexcept;
                MqttConnection(aws_mqtt_client* client, const char* hostName, uint16_t port,
                               const Io::SocketOptions& socketOptions) noexcept;

                aws_mqtt_client* m_owningClient;
                aws_mqtt_client_connection* m_underlyingConnection;
                std::atomic<ConnectionState> m_connectionState;

                static void s_onConnectionFailed(aws_mqtt_client_connection* connection, int errorCode, void* userData);
                static void s_onConnAck(aws_mqtt_client_connection* connection,
                    enum aws_mqtt_connect_return_code return_code,
                    bool session_present,
                    void* user_data);
                static bool s_onDisconnect(aws_mqtt_client_connection* connection, int errorCode, void* userData);
                static void s_onPublish(aws_mqtt_client_connection*connection,
                                        const aws_byte_cursor* topic,
                                        const aws_byte_cursor* payload,
                                        void* user_data);
                static void s_onOpComplete(aws_mqtt_client_connection* connection, uint16_t packetId, void* userdata);
                static void s_onSubAck(aws_mqtt_client_connection* connection, uint16_t packetId, 
                    const struct aws_byte_cursor *topic, enum aws_mqtt_qos qos,  int error_code, void* userdata);
                static void s_onMultiSubAck(aws_mqtt_client_connection* connection, uint16_t packetId, 
                    const struct aws_array_list *topic_subacks, int error_code, void* userdata);
                static void s_onOpComplete(aws_mqtt_client_connection* connection, uint16_t packetId, int errorCode, void* userdata);

                static void s_connectionInit(MqttConnection* self, const char* hostName, uint16_t port,
                    const Io::SocketOptions& socketOptions, const Io::TlsConnectionOptions* tlsConnOptions);
            };

            /**
             * An MQTT client. This is a move-only type. Unless otherwise specified,  
             * all function arguments need only to live through the duration of the 
             * function call.
             */
            class AWS_CRT_CPP_API MqttClient final
            {
            public:
                /**
                 * Initialize an MqttClient using bootstrap and allocator
                 */
                MqttClient(Io::ClientBootstrap& bootstrap, Allocator* allocator = DefaultAllocator()) noexcept;

                ~MqttClient();
                MqttClient(const MqttClient&) = delete;
                MqttClient(MqttClient&&) noexcept;
                MqttClient& operator =(const MqttClient&) = delete;
                MqttClient& operator =(MqttClient&&) noexcept;

                operator bool() const noexcept;
                int LastError() const noexcept;

                /**
                 * Create a new connection object using TLS from the client. The client must outlive
                 * all of its connection instances.
                 */
                std::shared_ptr<MqttConnection> NewConnection(const char* hostName, uint16_t port,
                        const Io::SocketOptions& socketOptions, const Io::TlsConnectionOptions& tlsConnOptions) noexcept;
                /**
                * Create a new connection object over plain text from the client. The client must outlive
                * all of its connection instances.
                */
                std::shared_ptr<MqttConnection> NewConnection(const char* hostName, uint16_t port,
                    const Io::SocketOptions& socketOptions) noexcept;

            private:
                aws_mqtt_client* m_client;
            };
        }
    }
}
