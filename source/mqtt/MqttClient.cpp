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

#include <aws/crt/StlAllocator.h>
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

                connWrapper->m_connectionState = ConnectionState::Disconnected;
                if (connWrapper->m_onConnectionFailed)
                {
                    connWrapper->m_onConnectionFailed(*connWrapper, errorCode);
                }
            }

            void MqttConnection::s_onConnAck(aws_mqtt_client_connection*,
                    enum aws_mqtt_connect_return_code returnCode,
                    bool sessionPresent,
                    void* userData)
            {
                auto connWrapper = reinterpret_cast<MqttConnection*>(userData);

                if (returnCode == AWS_MQTT_CONNECT_ACCEPTED)
                {
                    connWrapper->m_connectionState = ConnectionState::Connected;
                }
                else
                {
                    connWrapper->m_connectionState = ConnectionState::Error;
                }

                if (connWrapper->m_onConnAck)
                {
                    connWrapper->m_onConnAck(*connWrapper, returnCode, sessionPresent);
                }
            }

            bool MqttConnection::s_onDisconnect(aws_mqtt_client_connection*, int errorCode, void* userData)
            {
                auto connWrapper = reinterpret_cast<MqttConnection*>(userData);

                connWrapper->m_connectionState = ConnectionState::Disconnected;

                if (connWrapper->m_onDisconnect)
                {
                    if (connWrapper->m_onDisconnect(*connWrapper, errorCode))
                    {
                        connWrapper->m_connectionState = ConnectionState::Connecting;
                        return true;
                    }                    
                }

                return false;
            }

            struct PubCallbackData
            {
                MqttConnection* connection;
                OnPublishReceivedHandler onPublishReceived;
                Allocator* allocator;
            };

            static void s_cleanUpOnPublishData(void *userData)
            {
                auto callbackData = reinterpret_cast<PubCallbackData*>(userData);
                callbackData->~PubCallbackData();
                aws_mem_release(callbackData->allocator, reinterpret_cast<void*>(callbackData));
            }

            void MqttConnection::s_onPublish(aws_mqtt_client_connection*,
                                    const aws_byte_cursor* topic,
                                    const aws_byte_cursor* payload,
                                    void* userData)
            {
                auto callbackData = reinterpret_cast<PubCallbackData*>(userData);

                if (callbackData->onPublishReceived)
                {
                    ByteBuf topicBuf = aws_byte_buf_from_array(topic->ptr, topic->len);
                    ByteBuf payloadBuf = aws_byte_buf_from_array(payload->ptr, payload->len);
                    callbackData->onPublishReceived(*(callbackData->connection), topicBuf, payloadBuf);
                }
            }

            struct OpCompleteCallbackData
            {
                MqttConnection* connection;
                OnOperationCompleteHandler onOperationComplete;
                const char* topic;
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

                if (callbackData->topic)
                {
                    aws_mem_release(callbackData->allocator,
                            reinterpret_cast<void*>(const_cast<char*>(callbackData->topic)));

                }
                callbackData->~OpCompleteCallbackData();
                aws_mem_release(callbackData->allocator, reinterpret_cast<void *>(callbackData));
            }

            void MqttConnection::s_connectionInit(MqttConnection* self,
                const char* hostName, uint16_t port, const Io::SocketOptions& socketOptions, 
                Io::TlsConnectionOptions* tlsConnOptions)
            {
                aws_mqtt_client_connection_callbacks callbacks;
                AWS_ZERO_STRUCT(callbacks);
                callbacks.user_data = self;
                callbacks.on_connack = s_onConnAck;
                callbacks.on_connection_failed = s_onConnectionFailed;
                callbacks.on_disconnect = s_onDisconnect;

                ByteBuf hostNameBuf = aws_byte_buf_from_c_str(hostName);
                ByteCursor hostNameCur = aws_byte_cursor_from_buf(&hostNameBuf);

                self->m_underlyingConnection = aws_mqtt_client_connection_new(self->m_owningClient, 
                                                  callbacks, &hostNameCur, port,
                                                  const_cast<Io::SocketOptions*>(&socketOptions), tlsConnOptions);

                if (!self->m_underlyingConnection)
                {
                    self->m_connectionState = ConnectionState::Error;
                }
            }

            MqttConnection::MqttConnection(aws_mqtt_client* client,
                        const char* hostName, uint16_t port,
                        const Io::SocketOptions& socketOptions,
                        Io::TlsConnectionOptions&& tlsConnOptions) noexcept :
                           m_owningClient(client),
                           m_connectionState(ConnectionState::Init)
            {
                s_connectionInit(this, hostName, port, socketOptions, &tlsConnOptions);
            } 

            MqttConnection::MqttConnection(aws_mqtt_client* client,
                    const char* hostName, uint16_t port,
                    const Io::SocketOptions& socketOptions) noexcept :
                m_owningClient(client),
                m_connectionState(ConnectionState::Init)
            {
                s_connectionInit(this, hostName, port, socketOptions, nullptr);
            }

            MqttConnection::~MqttConnection()
            {
                if (*this)
                {
                    aws_mqtt_client_connection_destroy(m_underlyingConnection);
                }

                AWS_ZERO_STRUCT(*this);
            }           

            MqttConnection::operator bool() const noexcept
            {
                return m_connectionState != ConnectionState::Error;
            }

            int MqttConnection::LastError() const noexcept
            {
                return aws_last_error();
            }

            bool MqttConnection::SetWill(const char* topic, QOS qos, bool retain,
                         const ByteBuf& payload) noexcept
            {
                ByteBuf topicBuf = aws_byte_buf_from_c_str(topic);
                ByteCursor topicCur = aws_byte_cursor_from_buf(&topicBuf);
                ByteCursor payloadCur = aws_byte_cursor_from_buf(&payload);

                if (aws_mqtt_client_connection_set_will(m_underlyingConnection, &topicCur, qos, retain, &payloadCur))
                {                    
                    return false;
                }

                return true;
            }

            bool MqttConnection::SetLogin(const char* userName, const char* password) noexcept
            {
                ByteBuf userNameBuf = aws_byte_buf_from_c_str(userName);
                ByteCursor userNameCur = aws_byte_cursor_from_buf(&userNameBuf);
                ByteBuf pwdBuf = aws_byte_buf_from_c_str(password);
                ByteCursor pwdCur = aws_byte_cursor_from_buf(&pwdBuf);
                if (aws_mqtt_client_connection_set_login(m_underlyingConnection, &userNameCur, &pwdCur))
                {
                    return false;
                }

                return true;
            }

            bool MqttConnection::Connect(const char* clientId, bool cleanSession,
                    uint16_t keepAliveTime) noexcept
            {
                ByteBuf clientIdBuf = aws_byte_buf_from_c_str(clientId);
                ByteCursor clientIdCur = aws_byte_cursor_from_buf(&clientIdBuf);

                if (aws_mqtt_client_connection_connect(m_underlyingConnection,
                        &clientIdCur, cleanSession, keepAliveTime))
                {
                    return false;
                }
                m_connectionState = ConnectionState::Connecting;
                return true;             
            }

            bool MqttConnection::Disconnect() noexcept
            {
                if (aws_mqtt_client_connection_disconnect(m_underlyingConnection))
                {
                    return false;
                }

                return true;
            }

            uint16_t MqttConnection::Subscribe(const char* topicFilter, QOS qos,
                               OnPublishReceivedHandler&& onPublish,
                               OnOperationCompleteHandler&& onOpComplete) noexcept
            {                

                PubCallbackData* pubCallbackData =
                        reinterpret_cast<PubCallbackData*>(aws_mem_acquire(m_owningClient->allocator,
                                sizeof(PubCallbackData)));

                if (!pubCallbackData)
                {
                    return 0;
                }
                pubCallbackData = new(pubCallbackData)PubCallbackData;

                pubCallbackData->connection = this;
                pubCallbackData->onPublishReceived = std::move(onPublish);
                pubCallbackData->allocator = m_owningClient->allocator;

                OpCompleteCallbackData *opCompleteCallbackData =
                        reinterpret_cast<OpCompleteCallbackData*>(aws_mem_acquire(m_owningClient->allocator,
                                sizeof(OpCompleteCallbackData)));

                if (!opCompleteCallbackData)
                {
                    pubCallbackData->~PubCallbackData();
                    aws_mem_release(m_owningClient->allocator, reinterpret_cast<void*>(pubCallbackData));
                    return 0;
                }

                opCompleteCallbackData = new(opCompleteCallbackData)OpCompleteCallbackData;

                opCompleteCallbackData->connection = this;
                opCompleteCallbackData->allocator = m_owningClient->allocator;
                opCompleteCallbackData->onOperationComplete = std::move(onOpComplete);
                opCompleteCallbackData->topic = nullptr;
                opCompleteCallbackData->allocator = m_owningClient->allocator;

                ByteBuf topicFilterBuf = aws_byte_buf_from_c_str(topicFilter);
                ByteCursor topicFilterCur = aws_byte_cursor_from_buf(&topicFilterBuf);

                uint16_t packetId = aws_mqtt_client_connection_subscribe(m_underlyingConnection,
                        &topicFilterCur, qos, s_onPublish,
                        pubCallbackData, s_cleanUpOnPublishData, s_onOpComplete, opCompleteCallbackData);

                if (!packetId)
                {
                    pubCallbackData->~PubCallbackData();
                    aws_mem_release(m_owningClient->allocator, reinterpret_cast<void*>(pubCallbackData));
                    opCompleteCallbackData->~OpCompleteCallbackData();
                    aws_mem_release(m_owningClient->allocator,
                            reinterpret_cast<void*>(opCompleteCallbackData));
                }

                return packetId;
            }

            uint16_t MqttConnection::Unsubscribe(const char* topicFilter,
                    OnOperationCompleteHandler&& onOpComplete) noexcept
            {
                OpCompleteCallbackData *opCompleteCallbackData =
                        reinterpret_cast<OpCompleteCallbackData*>(aws_mem_acquire(m_owningClient->allocator,
                                                                 sizeof(OpCompleteCallbackData)));
                if (!opCompleteCallbackData)
                {
                    return 0;
                }

                opCompleteCallbackData = new(opCompleteCallbackData)OpCompleteCallbackData;

                opCompleteCallbackData->connection = this;
                opCompleteCallbackData->allocator = m_owningClient->allocator;
                opCompleteCallbackData->onOperationComplete = std::move(onOpComplete);
                opCompleteCallbackData->topic = nullptr;
                ByteBuf topicFilterBuf = aws_byte_buf_from_c_str(topicFilter);
                ByteCursor topicFilterCur = aws_byte_cursor_from_buf(&topicFilterBuf);

                uint16_t packetId = aws_mqtt_client_connection_unsubscribe(m_underlyingConnection, &topicFilterCur,
                                                                           s_onOpComplete, opCompleteCallbackData);

                if (!packetId)
                {
                    opCompleteCallbackData->~OpCompleteCallbackData();
                    aws_mem_release(m_owningClient->allocator,
                            reinterpret_cast<void*>(opCompleteCallbackData));
                }

                return packetId;
            }

            uint16_t MqttConnection::Publish(const char* topic, QOS qos, bool retain, const ByteBuf& payload,
                             OnOperationCompleteHandler&& onOpComplete) noexcept
            {

                OpCompleteCallbackData *opCompleteCallbackData =
                        reinterpret_cast<OpCompleteCallbackData*>(aws_mem_acquire(m_owningClient->allocator,
                                                                 sizeof(OpCompleteCallbackData)));
                if (!opCompleteCallbackData)
                {
                    return 0;
                }
                opCompleteCallbackData = new(opCompleteCallbackData)OpCompleteCallbackData;

                size_t topicLen = strlen(topic) + 1;
                char* topicCpy =
                        reinterpret_cast<char*>(aws_mem_acquire(m_owningClient->allocator,
                                sizeof(char) * (topicLen)));

                if (!topicCpy)
                {
                    opCompleteCallbackData->~OpCompleteCallbackData();
                    aws_mem_release(m_owningClient->allocator, opCompleteCallbackData);
                }

                memcpy(topicCpy, topic, topicLen);

                opCompleteCallbackData->connection = this;
                opCompleteCallbackData->allocator = m_owningClient->allocator;
                opCompleteCallbackData->onOperationComplete = std::move(onOpComplete);
                opCompleteCallbackData->topic = topicCpy;
                ByteCursor topicCur = aws_byte_cursor_from_array(topicCpy, topicLen - 1);

                ByteCursor payloadCur = aws_byte_cursor_from_buf(&payload);
                uint16_t packetId = aws_mqtt_client_connection_publish(m_underlyingConnection, &topicCur, qos,
                        retain, &payloadCur, s_onOpComplete, opCompleteCallbackData);

                if (!packetId)
                {
                    aws_mem_release(m_owningClient->allocator, reinterpret_cast<void*>(topicCpy));
                    opCompleteCallbackData->~OpCompleteCallbackData();
                    aws_mem_release(m_owningClient->allocator,
                            reinterpret_cast<void*>(opCompleteCallbackData));
                }

                return packetId;
            }

            void MqttConnection::Ping()
            {
                aws_mqtt_client_connection_ping(m_underlyingConnection);
            }

            MqttClient::MqttClient(Io::ClientBootstrap &bootstrap, Allocator *allocator) noexcept :
                    m_client(nullptr)
            {
                m_client = reinterpret_cast<aws_mqtt_client*>(aws_mem_acquire(allocator, sizeof(struct aws_mqtt_client)));
                if (!m_client)
                {
                    return;
                }
                
                if (aws_mqtt_client_init(m_client, allocator, bootstrap.GetUnderlyingHandle()))
                {
                    aws_mem_release(allocator, reinterpret_cast<void*>(m_client));
                    m_client = nullptr;
                }
            }

            MqttClient::~MqttClient()
            {
                if (m_client)
                {
                    Allocator* allocator = m_client->allocator;
                    aws_mqtt_client_clean_up(m_client);
                    aws_mem_release(allocator, reinterpret_cast<void*>(m_client));
                    m_client = nullptr;
                }
            }

            MqttClient::MqttClient(MqttClient&& toMove) noexcept :
                m_client(toMove.m_client)
            {
                toMove.m_client = nullptr;
            }

            MqttClient& MqttClient::operator =(MqttClient&& toMove) noexcept
            {
                if (&toMove != this)
                {
                    m_client = toMove.m_client;
                    toMove.m_client = nullptr;
                }

                return *this;
            }

            MqttClient::operator bool() const noexcept
            {
                return m_client != nullptr;
            }

            int MqttClient::LastError() const noexcept
            {
                return aws_last_error();
            }
           
            std::shared_ptr<MqttConnection> MqttClient::NewConnection(const char* hostName, uint16_t port,
                                         const Io::SocketOptions& socketOptions,
                                         Io::TlsConnectionOptions&& tlsConnOptions) noexcept
            {
                // If you're reading this and asking.... why is this so complicated? Why not use make_shared
                // or allocate_shared? Well, MqttConnection constructors are private and stl is dumb like that.
                // so, we do it manually.
                Allocator* allocator = m_client->allocator;
                MqttConnection* toSeat = 
                    reinterpret_cast<MqttConnection*>(aws_mem_acquire(allocator, sizeof(MqttConnection)));
                if (!toSeat)
                {
                    return nullptr;
                }

                toSeat = new(toSeat)MqttConnection(m_client, hostName, port, socketOptions,
                    std::forward<Io::TlsConnectionOptions>(tlsConnOptions));
                return std::shared_ptr<MqttConnection>(toSeat, 
                    [allocator](MqttConnection* connection)
                    {   
                        connection->~MqttConnection();
                        aws_mem_release(allocator, reinterpret_cast<void*>(connection)); 
                    }
                );
            }

            std::shared_ptr<MqttConnection> MqttClient::NewConnection(const char* hostName, uint16_t port,
                const Io::SocketOptions& socketOptions) noexcept

            {
                // If you're reading this and asking.... why is this so complicated? Why not use make_shared
                // or allocate_shared? Well, MqttConnection constructors are private and stl is dumb like that.
                // so, we do it manually.
                Allocator* allocator = m_client->allocator;
                MqttConnection* toSeat =
                    reinterpret_cast<MqttConnection*>(aws_mem_acquire(m_client->allocator, sizeof(MqttConnection)));
                if (!toSeat)
                {
                    return nullptr;
                }

                toSeat = new(toSeat)MqttConnection(m_client, hostName, port, socketOptions);
                return std::shared_ptr<MqttConnection>(toSeat,
                    [allocator](MqttConnection* connection)
                    {  
                        connection->~MqttConnection();
                        aws_mem_release(allocator, reinterpret_cast<void*>(connection));
                    }
                );
            }
        }
    }
}
