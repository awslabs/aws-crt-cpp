#pragma once

#include <aws/crt/Types.h>

#include <functional>
#include <memory>

namespace Aws
{
    namespace Crt
    {
        namespace Http
        {
            class HttpRequest;
        }

        namespace Mqtt
        {
            class MqttConnection;

            /**
             * The data returned when the connection closed callback is invoked in a connection.
             * Note: This class is currently empty, but this may contain data in the future.
             */
            struct OnConnectionClosedData
            {
            };

            /**
             * The data returned when the connection success callback is invoked in a connection.
             */
            struct OnConnectionSuccessData
            {
                /**
                 * The Connect return code received from the server.
                 */
                ReturnCode returnCode;

                /**
                 * Returns whether a session was present and resumed for this successful connection.
                 * Will be set to true if the connection resumed an already present MQTT connection session.
                 */
                bool sessionPresent;
            };

            /**
             * The data returned when the connection failure callback is invoked in a connection.
             */
            struct OnConnectionFailureData
            {
                /**
                 * The AWS CRT error code for the connection failure.
                 * Use Aws::Crt::ErrorDebugString to get a human readable string from the error code.
                 */
                int error;
            };

            /**
             * Invoked Upon Connection loss.
             */
            using OnConnectionInterruptedHandler = std::function<void(MqttConnection &connection, int error)>;

            /**
             * Invoked Upon Connection resumed.
             */
            using OnConnectionResumedHandler =
                std::function<void(MqttConnection &connection, ReturnCode connectCode, bool sessionPresent)>;

            /**
             * Invoked when a connack message is received, or an error occurred.
             */
            using OnConnectionCompletedHandler = std::function<
                void(MqttConnection &connection, int errorCode, ReturnCode returnCode, bool sessionPresent)>;

            /**
             * Invoked when a connection is disconnected and shutdown successfully.
             *
             * Note: Currently callbackData will always be nullptr, but this may change in the future to send additional
             * data.
             */
            using OnConnectionClosedHandler =
                std::function<void(MqttConnection &connection, OnConnectionClosedData *callbackData)>;

            /**
             * Invoked whenever the connection successfully connects.
             *
             * This callback is invoked for every successful connect and every successful reconnect.
             */
            using OnConnectionSuccessHandler =
                std::function<void(MqttConnection &connection, OnConnectionSuccessData *callbackData)>;

            /**
             * Invoked whenever the connection fails to connect.
             *
             * This callback is invoked for every failed connect and every failed reconnect.
             */
            using OnConnectionFailureHandler =
                std::function<void(MqttConnection &connection, OnConnectionFailureData *callbackData)>;

            /**
             * Invoked when a suback message is received.
             */
            using OnSubAckHandler = std::function<
                void(MqttConnection &connection, uint16_t packetId, const String &topic, QOS qos, int errorCode)>;

            /**
             * Invoked when a suback message for multiple topics is received.
             */
            using OnMultiSubAckHandler = std::function<void(
                MqttConnection &connection,
                uint16_t packetId,
                const Vector<String> &topics,
                QOS qos,
                int errorCode)>;

            /**
             * Invoked when a disconnect message has been sent.
             */
            using OnDisconnectHandler = std::function<void(MqttConnection &connection)>;

            /**
             * Invoked upon receipt of a Publish message on a subscribed topic.
             * - connection:    The connection object
             * - topic:         The information channel to which the payload data was published.
             * - payload:       The payload data.
             * - dup:           DUP flag. If true, this might be re-delivery of an earlier
             *                      attempt to send the message.
             * - qos:           Quality of Service used to deliver the message.
             * - retain:        Retain flag. If true, the message was sent as a result of
             *                      a new subscription being made by the client.
             */
            using OnMessageReceivedHandler = std::function<void(
                MqttConnection &connection,
                const String &topic,
                const ByteBuf &payload,
                bool dup,
                QOS qos,
                bool retain)>;

            /**
             * @deprecated Use OnMessageReceivedHandler
             */
            using OnPublishReceivedHandler =
                std::function<void(MqttConnection &connection, const String &topic, const ByteBuf &payload)>;

            /**
             * Invoked when an operation completes.  For QoS 0, this is when the packet is passed to the tls
             * layer.  For QoS 1 (and 2, in theory) this is when the final ack packet is received from the server.
             */
            using OnOperationCompleteHandler =
                std::function<void(MqttConnection &connection, uint16_t packetId, int errorCode)>;

            /**
             * Callback for users to invoke upon completion of, presumably asynchronous, OnWebSocketHandshakeIntercept
             * callback's initiated process.
             */
            using OnWebSocketHandshakeInterceptComplete =
                std::function<void(const std::shared_ptr<Http::HttpRequest> &, int errorCode)>;

            /**
             * Invoked during websocket handshake to give users opportunity to transform an http request for purposes
             * such as signing/authorization etc... Returning from this function does not continue the websocket
             * handshake since some work flows may be asynchronous. To accommodate that, onComplete must be invoked upon
             * completion of the signing process.
             */
            using OnWebSocketHandshakeIntercept = std::function<
                void(std::shared_ptr<Http::HttpRequest> req, const OnWebSocketHandshakeInterceptComplete &onComplete)>;

            /* Simple statistics about the current state of the client's queue of operations */
            struct AWS_CRT_CPP_API MqttConnectionOperationStatistics
            {
                /*
                 * total number of operations submitted to the connection that have not yet been completed.  Unacked
                 * operations are a subset of this.
                 */
                uint64_t incompleteOperationCount;

                /*
                 * total packet size of operations submitted to the connection that have not yet been completed. Unacked
                 * operations are a subset of this.
                 */
                uint64_t incompleteOperationSize;

                /*
                 * total number of operations that have been sent to the server and are waiting for a corresponding ACK
                 * before they can be completed.
                 */
                uint64_t unackedOperationCount;

                /*
                 * total packet size of operations that have been sent to the server and are waiting for a corresponding
                 * ACK before they can be completed.
                 */
                uint64_t unackedOperationSize;
            };
        }
    }
}
