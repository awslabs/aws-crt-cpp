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
#include <aws/crt/mqtt/MqttClient.h>

#include <aws/crt/io/Bootstrap.h>

#include <utility>

namespace Aws
{
    namespace Crt
    {
        namespace Mqtt
        {
            void MqttConnection::s_onConnectionFailed(aws_mqtt_client_connection*,
                    int errorCode, void* userData)
            {
                auto connWrapper = reinterpret_cast<MqttConnection*>(userData);
                connWrapper->m_lastError = errorCode;

                if (connWrapper->m_onConnectionFailed)
                {
                    connWrapper->m_onConnectionFailed(*connWrapper);
                }
            }

            void MqttConnection::s_onConnAck(aws_mqtt_client_connection*,
                    enum aws_mqtt_connect_return_code returnCode,
                    bool sessionPresent,
                    void* userData)
            {
                auto connWrapper = reinterpret_cast<MqttConnection*>(userData);

                if (connWrapper->m_onConnAck)
                {
                    connWrapper->m_onConnAck(*connWrapper, returnCode, sessionPresent);
                }
            }

            void MqttConnection::s_onDisconnect(aws_mqtt_client_connection*, int errorCode, void* userData)
            {
                auto connWrapper = reinterpret_cast<MqttConnection*>(userData);
                connWrapper->m_lastError = errorCode;

                if (connWrapper->m_onDisconnect)
                {
                    connWrapper->m_onDisconnect(*connWrapper);
                }
            }

            struct PubCallbackData
            {
                MqttConnection* connection;
                OnPublishReceivedHandler onPublishReceived;
            };

            void MqttConnection::s_onPublish(aws_mqtt_client_connection*,
                                    const aws_byte_cursor* topic,
                                    const aws_byte_cursor* payload,
                                    void* userData)
            {
                auto callbackData = reinterpret_cast<PubCallbackData*>(userData);
                //TODO:
                // SDK-5312 gives us a callback to free this, for now let it leak. When it's fixed comeback and handle.

                if (callbackData->onPublishReceived)
                {
                    std::string topicStr((const char*)topic->ptr, topic->len);
                    ByteBuf payloadBuf = aws_byte_buf_from_array(payload->ptr, payload->len);
                    callbackData->onPublishReceived(*(callbackData->connection), topicStr, payloadBuf);
                }
            }

            struct OpCompleteCallbackData
            {
                MqttConnection* connection;
                OnOperationCompleteHandler onOperationComplete;
                std::string topic;
                Allocator* allocator;
            };

            void MqttConnection::s_onOpComplete(aws_mqtt_client_connection*,
                    uint16_t packetId, void* userData)
            {
                auto callbackData = reinterpret_cast<OpCompleteCallbackData*>(userData);

                if (callbackData->onOperationComplete)
                {
                    callbackData->onOperationComplete(*callbackData->connection, packetId);
                }

                aws_mem_release(callbackData->allocator, (void *)callbackData);
            }

            MqttConnection::MqttConnection(MqttClient* client,
                        const std::string& hostName, uint16_t port,
                        const Io::SocketOptions& socketOptions,
                        Io::TlSConnectionOptions&& tlsConnOptions) noexcept :
                           m_owningClient(client),
                           m_lastError(AWS_ERROR_SUCCESS),
                           m_isInit(false)
            {
                aws_mqtt_client_connection_callbacks callbacks;
                AWS_ZERO_STRUCT(callbacks);
                callbacks.user_data = this;
                callbacks.on_connack = s_onConnAck;
                callbacks.on_connection_failed = s_onConnectionFailed;
                callbacks.on_disconnect = s_onDisconnect;

                ByteCursor hostNameCur = aws_byte_cursor_from_array(hostName.c_str(), hostName.length());

                m_underlyingConnection =
                        aws_mqtt_client_connection_new(&m_owningClient->m_client, callbacks,
                                &hostNameCur, port,
                                (Io::SocketOptions*)&socketOptions, &tlsConnOptions);

                if (!m_underlyingConnection)
                {
                    m_lastError = aws_last_error();
                }
                else
                {
                    m_isInit = true;
                }
            }

            MqttConnection::operator bool() const noexcept
            {
                return m_isInit && m_lastError == AWS_ERROR_SUCCESS;
            }

            int MqttConnection::LastError() const noexcept
            {
                return m_lastError;
            }

            void MqttConnection::SetWill(const std::string& topic, QOS qos, bool retain,
                         const ByteBuf& payload) noexcept
            {
                ByteCursor topicCur = aws_byte_cursor_from_array(topic.c_str(), topic.length());
                ByteCursor payloadCur = aws_byte_cursor_from_buf(&payload);

                if (aws_mqtt_client_connection_set_will(m_underlyingConnection, &topicCur, qos, retain, &payloadCur))
                {
                    m_lastError = aws_last_error();
                }
            }

            void MqttConnection::SetLogin(const std::string& userName, const std::string& password) noexcept
            {
                ByteCursor userNameCur = aws_byte_cursor_from_array(userName.c_str(), userName.length());
                ByteCursor pwdCur = aws_byte_cursor_from_array(password.c_str(), password.length());
                if (aws_mqtt_client_connection_set_login(m_underlyingConnection, &userNameCur, &pwdCur))
                {
                    m_lastError = aws_last_error();
                }
            }

            void MqttConnection::Connect(const std::string& clientId, bool cleanSession,
                    uint16_t keepAliveTime) noexcept
            {
                ByteCursor clientIdCur = aws_byte_cursor_from_array(clientId.c_str(), clientId.length());

                if (aws_mqtt_client_connection_connect(m_underlyingConnection, &clientIdCur, cleanSession, keepAliveTime))
                {
                    m_lastError = aws_last_error();
                }
            }

            void MqttConnection::Disconnect() noexcept
            {
                if (aws_mqtt_client_connection_disconnect(m_underlyingConnection))
                {
                    m_lastError = aws_last_error();
                }
            }

            uint16_t MqttConnection::Subscribe(const std::string& topicFilter, QOS qos,
                               OnPublishReceivedHandler&& onPublish,
                               OnOperationCompleteHandler&& onOpComplete) noexcept
            {                

                PubCallbackData* pubCallbackData =
                        (PubCallbackData*)aws_mem_acquire(m_owningClient->m_client.allocator, sizeof(PubCallbackData));

                if (!pubCallbackData)
                {
                    m_lastError = aws_last_error();
                    return 0;
                }
                pubCallbackData = new(pubCallbackData)PubCallbackData;

                pubCallbackData->connection = this;
                pubCallbackData->onPublishReceived = std::move(onPublish);

                OpCompleteCallbackData *opCompleteCallbackData =
                        (OpCompleteCallbackData*)aws_mem_acquire(m_owningClient->m_client.allocator,
                                sizeof(OpCompleteCallbackData));

                if (!opCompleteCallbackData)
                {
                    aws_mem_release(m_owningClient->m_client.allocator, pubCallbackData);
                    m_lastError = aws_last_error();
                    return 0;
                }
                opCompleteCallbackData = new(opCompleteCallbackData)OpCompleteCallbackData;

                opCompleteCallbackData->connection = this;
                opCompleteCallbackData->allocator = m_owningClient->m_client.allocator;
                opCompleteCallbackData->onOperationComplete = std::move(onOpComplete);
                opCompleteCallbackData->topic = topicFilter;
                ByteCursor topicFilterCur = aws_byte_cursor_from_array(opCompleteCallbackData->topic.c_str(), 
                    opCompleteCallbackData->topic.length());

                uint16_t packetId = aws_mqtt_client_connection_subscribe(m_underlyingConnection,
                        &topicFilterCur, qos, s_onPublish,
                        pubCallbackData, s_onOpComplete, opCompleteCallbackData);

                if (!packetId)
                {
                    m_lastError = aws_last_error();
                }

                return packetId;
            }

            uint16_t MqttConnection::Unsubscribe(const std::string& topicFilter,
                    OnOperationCompleteHandler&& onOpComplete) noexcept
            {
                OpCompleteCallbackData *opCompleteCallbackData =
                        (OpCompleteCallbackData*)aws_mem_acquire(m_owningClient->m_client.allocator,
                                                                 sizeof(OpCompleteCallbackData));
                if (!opCompleteCallbackData)
                {
                    m_lastError = aws_last_error();
                    return 0;
                }
                opCompleteCallbackData = new(opCompleteCallbackData)OpCompleteCallbackData;

                opCompleteCallbackData->connection = this;
                opCompleteCallbackData->allocator = m_owningClient->m_client.allocator;
                opCompleteCallbackData->onOperationComplete = std::move(onOpComplete);
                opCompleteCallbackData->topic = topicFilter;
                ByteCursor topicFilterCur = aws_byte_cursor_from_array(opCompleteCallbackData->topic.c_str(), 
                    opCompleteCallbackData->topic.length());

                uint16_t packetId = aws_mqtt_client_connection_unsubscribe(m_underlyingConnection, &topicFilterCur,
                                                                           s_onOpComplete, opCompleteCallbackData);

                if (!packetId)
                {
                    m_lastError = aws_last_error();
                }

                return packetId;
            }

            uint16_t MqttConnection::Publish(const std::string& topic, QOS qos, bool retain, const ByteBuf& payload,
                             OnOperationCompleteHandler&& onOpComplete) noexcept
            {

                OpCompleteCallbackData *opCompleteCallbackData =
                        (OpCompleteCallbackData*)aws_mem_acquire(m_owningClient->m_client.allocator,
                                                                 sizeof(OpCompleteCallbackData));
                if (!opCompleteCallbackData)
                {
                    m_lastError = aws_last_error();
                    return 0;
                }
                opCompleteCallbackData = new(opCompleteCallbackData)OpCompleteCallbackData;

                opCompleteCallbackData->connection = this;
                opCompleteCallbackData->allocator = m_owningClient->m_client.allocator;
                opCompleteCallbackData->onOperationComplete = std::move(onOpComplete);
                opCompleteCallbackData->topic = topic;
                ByteCursor topicCur = aws_byte_cursor_from_array(opCompleteCallbackData->topic.c_str(), 
                    opCompleteCallbackData->topic.length());

                ByteCursor payloadCur = aws_byte_cursor_from_buf(&payload);
                uint16_t packetId = aws_mqtt_client_connection_publish(m_underlyingConnection, &topicCur, qos,
                        retain, &payloadCur, s_onOpComplete, opCompleteCallbackData);

                if (!packetId)
                {
                    m_lastError = aws_last_error();
                }

                return packetId;
            }

            void MqttConnection::Ping()
            {
                aws_mqtt_client_connection_ping(m_underlyingConnection);
            }

            MqttClient::MqttClient(const Io::ClientBootstrap &bootstrap, Allocator *allocator) noexcept :
                    m_lastError(AWS_ERROR_SUCCESS),
                    m_isInit(false)
            {
                AWS_ZERO_STRUCT(m_client);
                if (aws_mqtt_client_init(&m_client, allocator,
                        (aws_client_bootstrap* )bootstrap.GetUnderlyingHandle()))
                {
                    m_lastError = aws_last_error();
                }
                else
                {
                    m_isInit = true;
                }
            }

            MqttClient::~MqttClient()
            {
                if (m_isInit)
                {
                    aws_mqtt_client_clean_up(&m_client);
                    m_isInit = false;
                }
            }

            MqttClient::MqttClient(MqttClient&& toMove) noexcept :
                m_client(toMove.m_client),
                m_lastError(toMove.m_lastError),
                m_isInit(toMove.m_isInit)
            {
                toMove.m_isInit = false;
                AWS_ZERO_STRUCT(toMove.m_client);
            }

            MqttClient& MqttClient::operator =(MqttClient&& toMove) noexcept
            {
                if (&toMove != this)
                {
                    m_client = toMove.m_client;
                    m_lastError = toMove.m_lastError;
                    m_isInit = toMove.m_isInit;
                    toMove.m_isInit = false;
                    AWS_ZERO_STRUCT(toMove.m_client);
                }

                return *this;
            }

            MqttClient::operator bool() const noexcept
            {
                return m_isInit && !m_lastError;
            }

            int MqttClient::LastError() const noexcept
            {
                return m_lastError;
            }

            MqttConnection MqttClient::NewConnection(const std::string& hostName, uint16_t port,
                                         const Io::SocketOptions& socketOptions,
                                         Io::TlSConnectionOptions&& tlsConnOptions) noexcept
            {
                return MqttConnection(this, hostName, port, socketOptions, std::move(tlsConnOptions));
            }
        }
    }
}