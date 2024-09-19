#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Exports.h>

#include <aws/crt/Allocator.h>
#include <aws/crt/Optional.h>
#include <aws/crt/Types.h>
#include <aws/crt/Variant.h>
#include <aws/mqtt/request-response/request_response_client.h>

#include <functional>

namespace Aws
{

    namespace Crt
    {
        namespace Mqtt
        {
            class MqttConnection;
        }

        namespace Mqtt5
        {
            class Mqtt5Client;
        }
    } // namespace Crt

    namespace Iot
    {
        namespace RequestResponse
        {
            /**
             * The type of change to the state of a streaming operation subscription
             */
            enum class SubscriptionStatusEventType
            {

                /**
                 * The streaming operation is successfully subscribed to its topic (filter)
                 */
                SubscriptionEstablished = ARRSSET_SUBSCRIPTION_ESTABLISHED,

                /**
                 * The streaming operation has temporarily lost its subscription to its topic (filter)
                 */
                SubscriptionLost = ARRSSET_SUBSCRIPTION_LOST,

                /**
                 * The streaming operation has entered a terminal state where it has given up trying to subscribe
                 * to its topic (filter).  This is always due to user error (bad topic filter or IoT Core permission
                 * policy).
                 */
                SubscriptionHalted = ARRSSET_SUBSCRIPTION_HALTED,
            };

            /**
             * An event that describes a change in subscription status for a streaming operation.
             */
            class AWS_CRT_CPP_API SubscriptionStatusEvent
            {
              public:
                SubscriptionStatusEvent() : m_type(SubscriptionStatusEventType::SubscriptionEstablished), m_errorCode(0)
                {
                }
                SubscriptionStatusEvent(const SubscriptionStatusEvent &rhs) = default;
                SubscriptionStatusEvent(SubscriptionStatusEvent &&rhs) = default;
                ~SubscriptionStatusEvent() = default;

                SubscriptionStatusEvent &operator=(const SubscriptionStatusEvent &rhs) = default;
                SubscriptionStatusEvent &operator=(SubscriptionStatusEvent &&rhs) = default;

                SubscriptionStatusEvent &WithType(SubscriptionStatusEventType type)
                {
                    m_type = type;
                    return *this;
                }

                SubscriptionStatusEvent &WithErrorCode(int errorCode)
                {
                    m_errorCode = errorCode;
                    return *this;
                }

                SubscriptionStatusEventType GetType() const { return m_type; }
                int GetErrorCode() const { return m_errorCode; }

              private:
                SubscriptionStatusEventType m_type;
                int m_errorCode;
            };

            using SubscriptionStatusEventHandler = std::function<void(SubscriptionStatusEvent &&)>;

            // @internal
            class AWS_CRT_CPP_API IncomingPublishEvent
            {
              public:
                IncomingPublishEvent() : m_payload() { AWS_ZERO_STRUCT(m_payload); }
                IncomingPublishEvent(const IncomingPublishEvent &rhs) = default;
                IncomingPublishEvent(IncomingPublishEvent &&rhs) = default;
                ~IncomingPublishEvent() = default;

                IncomingPublishEvent &operator=(const IncomingPublishEvent &rhs) = default;
                IncomingPublishEvent &operator=(IncomingPublishEvent &&rhs) = default;

                IncomingPublishEvent &WithPayload(Aws::Crt::ByteCursor payload)
                {
                    m_payload = payload;
                    return *this;
                }

                Aws::Crt::ByteCursor GetPayload() const { return m_payload; }

              private:
                Aws::Crt::ByteCursor m_payload;
            };

            using IncomingPublishEventHandler = std::function<void(IncomingPublishEvent &&)>;

            /**
             * Encapsulates a response to an AWS IoT Core MQTT-based service request
             *
             * @internal
             */
            class AWS_CRT_CPP_API UnmodeledResponse
            {
              public:
                UnmodeledResponse() : m_topic(), m_payload()
                {
                    AWS_ZERO_STRUCT(m_payload);
                    AWS_ZERO_STRUCT(m_topic);
                }
                UnmodeledResponse(const UnmodeledResponse &rhs) = default;
                UnmodeledResponse(UnmodeledResponse &&rhs) = default;
                ~UnmodeledResponse() = default;

                UnmodeledResponse &operator=(const UnmodeledResponse &rhs) = default;
                UnmodeledResponse &operator=(UnmodeledResponse &&rhs) = default;

                UnmodeledResponse &WithPayload(Aws::Crt::ByteCursor payload)
                {
                    m_payload = payload;
                    return *this;
                }

                UnmodeledResponse &WithTopic(Aws::Crt::ByteCursor topic)
                {
                    m_topic = topic;
                    return *this;
                }

                Aws::Crt::ByteCursor GetPayload() const { return m_payload; }

                Aws::Crt::ByteCursor GetTopic() const { return m_topic; }

              private:
                /**
                 * MQTT Topic that the response was received on.  Different topics map to different types within the
                 * service model, so we need this value in order to know what to deserialize the payload into.
                 */
                Aws::Crt::ByteCursor m_topic;

                /**
                 * Payload of the response that correlates to a submitted request.
                 */
                Aws::Crt::ByteCursor m_payload;
            };

            template <typename R, typename E> class Result
            {
              public:
                Result() = delete;
                Result(const Result &result) = default;
                Result(Result &&result) = default;

                explicit Result(const R &response) : m_rawResult(response) {}
                explicit Result(R &&response) : m_rawResult(std::move(response)) {}
                explicit Result(const E &error) : m_rawResult(error) {}
                explicit Result(E &&error) : m_rawResult(std::move(error)) {}

                ~Result() = default;

                Result &operator=(const Result &result) = default;
                Result &operator=(Result &&result) = default;

                Result &operator=(const R &response)
                {
                    this->m_rawResult = response;

                    return *this;
                }

                Result &operator=(R &&response)
                {
                    this->m_rawResult = std::move(response);

                    return *this;
                }

                Result &operator=(const E &error) { this->m_rawResult = error; }

                Result &operator=(E &&error)
                {
                    this->m_rawResult = std::move(error);

                    return *this;
                }

                bool IsSuccess() const { return m_rawResult.template holds_alternative<R>(); }

                const R &GetResponse() const
                {
                    AWS_FATAL_ASSERT(IsSuccess());

                    return m_rawResult.template get<R>();
                }

                const E &GetError() const
                {
                    AWS_FATAL_ASSERT(!IsSuccess());

                    return m_rawResult.template get<E>();
                }

              private:
                Aws::Crt::Variant<R, E> m_rawResult;
            };

            using UnmodeledResult = Result<UnmodeledResponse, int>;

            using UnmodeledResultHandler = std::function<void(UnmodeledResult &&)>;

            template <typename T> class StreamingOperationOptions
            {
              public:
                StreamingOperationOptions() = default;
                StreamingOperationOptions(const StreamingOperationOptions &rhs) = default;
                StreamingOperationOptions(StreamingOperationOptions &&rhs) = default;
                ~StreamingOperationOptions() = default;

                StreamingOperationOptions &operator=(const StreamingOperationOptions &rhs) = default;
                StreamingOperationOptions &operator=(StreamingOperationOptions &&rhs) = default;

                StreamingOperationOptions &WithSubscriptionStatusEventHandler(
                    const SubscriptionStatusEventHandler &handler)
                {
                    m_subscriptionStatusEventHandler = handler;
                    return *this;
                }

                StreamingOperationOptions &WithStreamHandler(const std::function<void(T &&)> &handler)
                {
                    m_streamHandler = handler;
                    return *this;
                }

                const SubscriptionStatusEventHandler &GetSubscriptionStatusEventHandler() const
                {
                    return m_subscriptionStatusEventHandler;
                }
                const std::function<void(T &&)> &GetStreamHandler() const { return m_streamHandler; }

              private:
                SubscriptionStatusEventHandler m_subscriptionStatusEventHandler;

                std::function<void(T &&)> m_streamHandler;
            };

            // @internal
            struct AWS_CRT_CPP_API StreamingOperationOptionsInternal
            {
              public:
                StreamingOperationOptionsInternal()
                    : subscriptionTopicFilter(), subscriptionStatusEventHandler(), incomingPublishEventHandler()
                {
                    AWS_ZERO_STRUCT(subscriptionTopicFilter);
                }

                Aws::Crt::ByteCursor subscriptionTopicFilter;

                SubscriptionStatusEventHandler subscriptionStatusEventHandler;

                IncomingPublishEventHandler incomingPublishEventHandler;
            };

            class AWS_CRT_CPP_API IStreamingOperation
            {
              public:
                virtual ~IStreamingOperation() = default;

                virtual void Open() = 0;
            };

            /**
             * MQTT-based request-response client configuration options
             */
            class AWS_CRT_CPP_API RequestResponseClientOptions
            {
              public:
                RequestResponseClientOptions() = default;
                RequestResponseClientOptions(const RequestResponseClientOptions &rhs) = default;
                RequestResponseClientOptions(RequestResponseClientOptions &&rhs) = default;
                ~RequestResponseClientOptions() = default;

                RequestResponseClientOptions &operator=(const RequestResponseClientOptions &rhs) = default;
                RequestResponseClientOptions &operator=(RequestResponseClientOptions &&rhs) = default;

                RequestResponseClientOptions &WithMaxRequestResponseSubscriptions(
                    uint32_t maxRequestResponseSubscriptions)
                {
                    m_maxRequestResponseSubscriptions = maxRequestResponseSubscriptions;
                    return *this;
                }

                RequestResponseClientOptions &WithMaxStreamingSubscriptions(uint32_t maxStreamingSubscriptions)
                {
                    m_maxStreamingSubscriptions = maxStreamingSubscriptions;
                    return *this;
                }

                RequestResponseClientOptions &WithOperationTimeoutInSeconds(uint32_t operationTimeoutInSeconds)
                {
                    m_operationTimeoutInSeconds = operationTimeoutInSeconds;
                    return *this;
                }

                uint32_t GetMaxRequestResponseSubscriptions() const { return m_maxRequestResponseSubscriptions; }
                uint32_t GetMaxStreamingSubscriptions() const { return m_maxStreamingSubscriptions; }
                uint32_t GetOperationTimeoutInSeconds() const { return m_operationTimeoutInSeconds; }

              private:
                /**
                 * Maximum number of subscriptions that the client will concurrently use for request-response operations
                 */
                uint32_t m_maxRequestResponseSubscriptions;

                /**
                 * Maximum number of subscriptions that the client will concurrently use for streaming operations
                 */
                uint32_t m_maxStreamingSubscriptions;

                /**
                 * Duration, in seconds, that a request-response operation will wait for completion before giving up
                 */
                uint32_t m_operationTimeoutInSeconds;
            };

            class AWS_CRT_CPP_API IMqttRequestResponseClient
            {
              public:
                virtual ~IMqttRequestResponseClient() = default;

                virtual int SubmitRequest(
                    const aws_mqtt_request_operation_options &requestOptions,
                    UnmodeledResultHandler &&resultHandler) = 0;

                virtual std::shared_ptr<IStreamingOperation> CreateStream(
                    const StreamingOperationOptionsInternal &options) = 0;
            };

            AWS_CRT_CPP_API std::shared_ptr<IMqttRequestResponseClient> NewClientFrom5(
                const Aws::Crt::Mqtt5::Mqtt5Client &protocolClient,
                const RequestResponseClientOptions &options,
                Aws::Crt::Allocator *allocator = Aws::Crt::ApiAllocator());

            AWS_CRT_CPP_API std::shared_ptr<IMqttRequestResponseClient> NewClientFrom311(
                const Aws::Crt::Mqtt::MqttConnection &protocolClient,
                const RequestResponseClientOptions &options,
                Aws::Crt::Allocator *allocator = Aws::Crt::ApiAllocator());

        } // namespace RequestResponse
    } // namespace Iot
} // namespace Aws