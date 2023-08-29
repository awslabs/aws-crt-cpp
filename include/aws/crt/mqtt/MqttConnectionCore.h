#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Exports.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/Types.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/io/SocketOptions.h>
#include <aws/crt/io/TlsOptions.h>
#include <aws/crt/mqtt/MqttTypes.h>

#include <aws/mqtt/client.h>
#include <aws/mqtt/v5/mqtt5_client.h>

#include <atomic>
#include <functional>
#include <memory>

namespace Aws
{
    namespace Crt
    {
        namespace Mqtt
        {
            class MqttConnection;

            class AWS_CRT_CPP_API MqttConnectionCore final : public std::enable_shared_from_this<MqttConnectionCore>
            {
                /**
                 * @internal
                 */
                class CreationKey
                {
                };

              public:
                /**
                 * @internal
                 * Constructor for MQTT311 connection with TLS.
                 */
                MqttConnectionCore(
                    CreationKey key,
                    aws_mqtt_client *client,
                    const char *hostName,
                    uint16_t port,
                    const Io::SocketOptions &socketOptions,
                    const Crt::Io::TlsContext &tlsContext,
                    bool useWebsocket) noexcept;

                /**
                 * @internal
                 * Constructor for MQTT311 connection.
                 */
                MqttConnectionCore(
                    CreationKey key,
                    aws_mqtt_client *client,
                    const char *hostName,
                    uint16_t port,
                    const Io::SocketOptions &socketOptions,
                    bool useWebsocket) noexcept;

                /**
                 * @internal
                 * Constructor for MQTT5 connection with TLS.
                 */
                MqttConnectionCore(
                    CreationKey key,
                    aws_mqtt5_client *mqtt5Client,
                    const char *hostName,
                    uint16_t port,
                    const Io::SocketOptions &socketOptions,
                    const Crt::Io::TlsConnectionOptions &tlsConnectionOptions,
                    bool useWebsocket,
                    Allocator *allocator) noexcept;

                /**
                 * @internal
                 * Constructor for MQTT5 connection.
                 */
                MqttConnectionCore(
                    CreationKey key,
                    aws_mqtt5_client *mqtt5Client,
                    const char *hostName,
                    uint16_t port,
                    const Io::SocketOptions &socketOptions,
                    bool useWebsocket,
                    Allocator *allocator) noexcept;

                ~MqttConnectionCore();
                MqttConnectionCore(const MqttConnectionCore &) = delete;
                MqttConnectionCore(MqttConnectionCore &&) = delete;
                MqttConnectionCore &operator=(const MqttConnectionCore &) = delete;
                MqttConnectionCore &operator=(MqttConnectionCore &&) = delete;

                static std::shared_ptr<MqttConnectionCore> s_Create(
                    aws_mqtt_client *client,
                    const char *hostName,
                    uint16_t port,
                    const Io::SocketOptions &socketOptions,
                    const Crt::Io::TlsContext &tlsContext,
                    bool useWebsocket,
                    Allocator *allocator);

                static std::shared_ptr<MqttConnectionCore> s_Create(
                    aws_mqtt_client *client,
                    const char *hostName,
                    uint16_t port,
                    const Io::SocketOptions &socketOptions,
                    bool useWebsocket,
                    Allocator *allocator);

                static std::shared_ptr<MqttConnectionCore> s_Create(
                    aws_mqtt5_client *mqtt5Client,
                    const char *hostName,
                    uint16_t port,
                    const Io::SocketOptions &socketOptions,
                    const Crt::Io::TlsConnectionOptions &tlsConnectionOptions,
                    bool useWebsocket,
                    Allocator *allocator);

                static std::shared_ptr<MqttConnectionCore> s_Create(
                    aws_mqtt5_client *mqtt5Client,
                    const char *hostName,
                    uint16_t port,
                    const Io::SocketOptions &socketOptions,
                    bool useWebsocket,
                    Allocator *allocator);

                /**
                 * @internal
                 * @return true if the instance is in a valid state, false otherwise.
                 */
                operator bool() const noexcept;

                /**
                 * @internal
                 * Initialize.
                 */
                void Initialize(const std::shared_ptr<MqttConnection> &connection);

                /**
                 * @internal
                 * Close connection.
                 */
                void Close();

                /**
                 * @internal
                 * @return the value of the last aws error encountered by operations on this instance.
                 */
                int LastError() const noexcept;

                /**
                 * Sets LastWill for the connection.
                 * @param topic topic the will message should be published to
                 * @param qos QOS the will message should be published with
                 * @param retain true if the will publish should be treated as a retained publish
                 * @param payload payload of the will message
                 * @return success/failure in setting the will
                 */
                bool SetWill(const char *topic, QOS qos, bool retain, const ByteBuf &payload) noexcept;

                /**
                 * Sets login credentials for the connection. The must get set before the Connect call
                 * if it is to be used.
                 * @param username user name to add to the MQTT CONNECT packet
                 * @param password password to add to the MQTT CONNECT packet
                 * @return success/failure
                 */
                bool SetLogin(const char *username, const char *password) noexcept;

                /**
                 * @deprecated Sets websocket proxy options. Replaced by SetHttpProxyOptions.
                 */
                bool SetWebsocketProxyOptions(const Http::HttpClientConnectionProxyOptions &proxyOptions) noexcept;

                /**
                 * Sets http proxy options. In order to use an http proxy with mqtt either
                 *   (1) Websockets are used
                 *   (2) Mqtt-over-tls is used and the ALPN list of the tls context contains a tag that resolves to mqtt
                 *
                 * @param proxyOptions proxy configuration for making the mqtt connection
                 *
                 * @return success/failure
                 */
                bool SetHttpProxyOptions(const Http::HttpClientConnectionProxyOptions &proxyOptions) noexcept;

                /**
                 * Customize time to wait between reconnect attempts.
                 * The time will start at min and multiply by 2 until max is reached.
                 * The time resets back to min after a successful connection.
                 * This function should only be called before Connect().
                 *
                 * @param min_seconds minimum time to wait before attempting a reconnect
                 * @param max_seconds maximum time to wait before attempting a reconnect
                 *
                 * @return success/failure
                 */
                bool SetReconnectTimeout(uint64_t min_seconds, uint64_t max_seconds) noexcept;

                /**
                 * Initiates the connection, OnConnectionCompleted will
                 * be invoked in an event-loop thread.
                 *
                 * @param clientId client identifier to use when establishing the mqtt connection
                 * @param cleanSession false to attempt to rejoin an existing session for the client id, true to skip
                 * and start with a new session
                 * @param keepAliveTimeSecs time interval to space mqtt pings apart by
                 * @param pingTimeoutMs timeout in milliseconds before the keep alive ping is considered to have failed
                 * @param protocolOperationTimeoutMs timeout in milliseconds to give up waiting for a response packet
                 * for an operation.  Necessary due to throttling properties on certain server implementations that do
                 * not return an ACK for throttled operations.
                 *
                 * @return true if the connection attempt was successfully started (implying a callback will be invoked
                 * with the eventual result), false if it could not be started (no callback will happen)
                 */
                bool Connect(
                    const char *clientId,
                    bool cleanSession,
                    uint16_t keepAliveTimeSecs = 0,
                    uint32_t pingTimeoutMs = 0,
                    uint32_t protocolOperationTimeoutMs = 0) noexcept;

                /**
                 * Initiates disconnect, OnDisconnectHandler will be invoked in an event-loop thread.
                 * @return success/failure in initiating disconnect
                 */
                bool Disconnect() noexcept;

                /// @private
                aws_mqtt_client_connection *GetUnderlyingConnection() noexcept;

                /**
                 * Subscribes to topicFilter. OnMessageReceivedHandler will be invoked from an event-loop
                 * thread upon an incoming Publish message. OnSubAckHandler will be invoked
                 * upon receipt of a suback message.
                 *
                 * @param topicFilter topic filter to subscribe to
                 * @param qos maximum qos client is willing to receive matching messages on
                 * @param onMessage callback to invoke when a message is received based on matching this filter
                 * @param onSubAck callback to invoke with the server's response to the subscribe request
                 *
                 * @return packet id of the subscribe request, or 0 if the attempt failed synchronously
                 */
                uint16_t Subscribe(
                    const char *topicFilter,
                    QOS qos,
                    OnMessageReceivedHandler &&onMessage,
                    OnSubAckHandler &&onSubAck) noexcept;

                /**
                 * @deprecated Use alternate Subscribe()
                 */
                uint16_t Subscribe(
                    const char *topicFilter,
                    QOS qos,
                    OnPublishReceivedHandler &&onPublish,
                    OnSubAckHandler &&onSubAck) noexcept;

                /**
                 * Subscribes to multiple topicFilters. OnMessageReceivedHandler will be invoked from an event-loop
                 * thread upon an incoming Publish message. OnMultiSubAckHandler will be invoked
                 * upon receipt of a suback message.
                 *
                 * @param topicFilters list of pairs of topic filters and message callbacks to invoke on a matching
                 * publish
                 * @param qos maximum qos client is willing to receive matching messages on
                 * @param onSubAck callback to invoke with the server's response to the subscribe request
                 *
                 * @return packet id of the subscribe request, or 0 if the attempt failed synchronously
                 */
                uint16_t Subscribe(
                    const Vector<std::pair<const char *, OnMessageReceivedHandler>> &topicFilters,
                    QOS qos,
                    OnMultiSubAckHandler &&onSubAck) noexcept;

                /**
                 * @deprecated Use alternate Subscribe()
                 */
                uint16_t Subscribe(
                    const Vector<std::pair<const char *, OnPublishReceivedHandler>> &topicFilters,
                    QOS qos,
                    OnMultiSubAckHandler &&onSubAck) noexcept;

                /**
                 * Installs a handler for all incoming publish messages, regardless of if Subscribe has been
                 * called on the topic.
                 *
                 * @param onMessage callback to invoke for all received messages
                 * @return success/failure
                 */
                bool SetOnMessageHandler(OnMessageReceivedHandler &&onMessage) noexcept;

                /**
                 * Unsubscribes from topicFilter. OnOperationCompleteHandler will be invoked upon receipt of
                 * an unsuback message.
                 *
                 * @param topicFilter topic filter to unsubscribe the session from
                 * @param onOpComplete callback to invoke on receipt of the server's UNSUBACK message
                 *
                 * @return packet id of the unsubscribe request, or 0 if the attempt failed synchronously
                 */
                uint16_t Unsubscribe(const char *topicFilter, OnOperationCompleteHandler &&onOpComplete) noexcept;

                /**
                 * Publishes to a topic.
                 *
                 * @param topic topic to publish to
                 * @param qos QOS to publish the message with
                 * @param retain should this message replace the current retained message of the topic?
                 * @param payload payload of the message
                 * @param onOpComplete completion callback to invoke when the operation is complete.  If QoS is 0, then
                 * the callback is invoked when the message is passed to the tls handler, otherwise it's invoked
                 * on receipt of the final response from the server.
                 *
                 * @return packet id of the publish request, or 0 if the attempt failed synchronously
                 */
                uint16_t Publish(
                    const char *topic,
                    QOS qos,
                    bool retain,
                    const ByteBuf &payload,
                    OnOperationCompleteHandler &&onOpComplete) noexcept;

                /**
                 * Get the statistics about the current state of the connection's queue of operations
                 *
                 * @return MqttConnectionOperationStatistics
                 */
                const MqttConnectionOperationStatistics &GetOperationStatistics() noexcept;

              private:
                static void s_onConnectionTermination(void *userData);

                static void s_onConnectionInterrupted(aws_mqtt_client_connection *, int errorCode, void *userData);
                static void s_onConnectionCompleted(
                    aws_mqtt_client_connection *,
                    int errorCode,
                    enum aws_mqtt_connect_return_code returnCode,
                    bool sessionPresent,
                    void *userData);

                static void s_onConnectionSuccess(
                    aws_mqtt_client_connection *,
                    ReturnCode returnCode,
                    bool sessionPresent,
                    void *userData);

                static void s_onConnectionFailure(aws_mqtt_client_connection *, int errorCode, void *userData);

                static void s_onConnectionResumed(
                    aws_mqtt_client_connection *,
                    ReturnCode returnCode,
                    bool sessionPresent,
                    void *userData);

                static void s_onConnectionClosed(
                    aws_mqtt_client_connection *,
                    on_connection_closed_data *data,
                    void *userData);

                static void s_onDisconnect(aws_mqtt_client_connection *connection, void *userData);
                static void s_onPublish(
                    aws_mqtt_client_connection *connection,
                    const aws_byte_cursor *topic,
                    const aws_byte_cursor *payload,
                    bool dup,
                    enum aws_mqtt_qos qos,
                    bool retain,
                    void *userData);

                static void s_onSubAck(
                    aws_mqtt_client_connection *connection,
                    uint16_t packetId,
                    const struct aws_byte_cursor *topic,
                    enum aws_mqtt_qos qos,
                    int errorCode,
                    void *userdata);
                static void s_onMultiSubAck(
                    aws_mqtt_client_connection *connection,
                    uint16_t packetId,
                    const struct aws_array_list *topicSubacks,
                    int errorCode,
                    void *userdata);
                static void s_onOpComplete(
                    aws_mqtt_client_connection *connection,
                    uint16_t packetId,
                    int errorCode,
                    void *userdata);

                static void s_onWebsocketHandshake(
                    struct aws_http_message *request,
                    void *userData,
                    aws_mqtt_transform_websocket_handshake_complete_fn *completeFn,
                    void *completeCtx);

                static void s_connectionInit(
                    MqttConnectionCore *self,
                    const char *hostName,
                    uint16_t port,
                    const Io::SocketOptions &socketOptions,
                    aws_mqtt5_client *mqtt5Client = nullptr);

                aws_mqtt_client *m_owningClient;
                aws_mqtt_client_connection *m_underlyingConnection;
                String m_hostName;
                uint16_t m_port;
                Crt::Io::TlsContext m_tlsContext;
                Io::TlsConnectionOptions m_tlsOptions;
                Io::SocketOptions m_socketOptions;
                Crt::Optional<Http::HttpClientConnectionProxyOptions> m_proxyOptions;
                void *m_onAnyCbData;
                bool m_useTls;
                bool m_useWebsocket;
                MqttConnectionOperationStatistics m_operationStatistics;
                Allocator *m_allocator;

                /** @internal */
                std::mutex m_connectionMutex;
                /** @internal */
                bool m_isConnectionAlive;
                /** @internal */
                std::weak_ptr<MqttConnection> m_connection;
                /** @internal */
                std::shared_ptr<MqttConnectionCore> m_self;
                /** @internal */
                bool m_isInitialized = false;
            };
        } // namespace Mqtt
    }     // namespace Crt
} // namespace Aws
