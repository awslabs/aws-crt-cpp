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

            class MqttRequestResponseClientImpl;

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
            struct AWS_CRT_CPP_API SubscriptionStatusEvent
            {
                SubscriptionStatusEventType type;
                int errorCode;
            };

            using SubscriptionStatusEventHandler = std::function<void(SubscriptionStatusEvent &&)>;

            struct AWS_CRT_CPP_API IncomingPublishEvent
            {
                Aws::Crt::ByteCursor payload;
            };

            using IncomingPublishEventHandler = std::function<void(IncomingPublishEvent &&)>;

            /**
             * Encapsulates a response to an AWS IoT Core MQTT-based service request
             */
            struct AWS_CRT_CPP_API UnmodeledResponse
            {

                /**
                 * MQTT Topic that the response was received on.  Different topics map to different types within the
                 * service model, so we need this value in order to know what to deserialize the payload into.
                 */
                Aws::Crt::ByteCursor topic;

                /**
                 * Payload of the response that correlates to a submitted request.
                 */
                Aws::Crt::ByteCursor payload;
            };

            template <typename R, typename E> struct Result
            {
              public:
                Result() = delete;
                Result(const Result &result) = default;
                Result(Result &&result) = default;

                Result(const R &response) : rawResult(response) {}

                Result(R &&response) : rawResult(std::move(response)) {}

                Result(const E &error) : rawResult(error) {}

                Result(E &&error) : rawResult(std::move(error)) {}

                ~Result() = default;

                Result &operator=(const Result &result) = default;
                Result &operator=(Result &&result) = default;

                Result &operator=(const R &response)
                {
                    this->rawResult = response;

                    return *this;
                }

                Result &operator=(R &&response)
                {
                    this->rawResult = std::move(response);

                    return *this;
                }

                Result &operator=(const E &error) { this->rawResult = error; }

                Result &operator=(E &&error)
                {
                    this->rawResult = std::move(error);

                    return *this;
                }

                bool isSuccess() const { return rawResult.template holds_alternative<R>(); }

                const R &getResponse() const
                {
                    AWS_FATAL_ASSERT(isSuccess());

                    return rawResult.template get<R>();
                }

                const E &getError() const
                {
                    AWS_FATAL_ASSERT(!isSuccess());

                    return rawResult.template get<E>();
                }

              private:
                Aws::Crt::Variant<R, E> rawResult;
            };

            using UnmodeledResult = Result<UnmodeledResponse, int>;

            using UnmodeledResultHandler = std::function<void(UnmodeledResult &&)>;

            struct AWS_CRT_CPP_API StreamingOperationOptions
            {
                Aws::Crt::ByteCursor subscriptionTopicFilter;

                SubscriptionStatusEventHandler subscriptionStatusEventHandler;

                IncomingPublishEventHandler incomingPublishEventHandler;
            };

            class AWS_CRT_CPP_API IStreamingOperation
            {
              public:
                virtual ~IStreamingOperation() = default;

                virtual void activate() = 0;
            };

            /**
             * MQTT-based request-response client configuration options
             */
            struct AWS_CRT_CPP_API RequestResponseClientOptions
            {

                /**
                 * Maximum number of subscriptions that the client will concurrently use for request-response operations
                 */
                uint32_t maxRequestResponseSubscriptions;

                /**
                 * Maximum number of subscriptions that the client will concurrently use for streaming operations
                 */
                uint32_t maxStreamingSubscriptions;

                /**
                 * Duration, in seconds, that a request-response operation will wait for completion before giving up
                 */
                uint32_t operationTimeoutInSeconds;
            };

            class AWS_CRT_CPP_API IMqttRequestResponseClient
            {
              public:
                virtual ~IMqttRequestResponseClient() = default;

                virtual int submitRequest(
                    const aws_mqtt_request_operation_options &requestOptions,
                    UnmodeledResultHandler &&resultHandler) = 0;

                virtual std::shared_ptr<IStreamingOperation> createStream(StreamingOperationOptions &&options) = 0;

                static IMqttRequestResponseClient *newFrom5(
                    const Aws::Crt::Mqtt5::Mqtt5Client &protocolClient,
                    const RequestResponseClientOptions &options,
                    Aws::Crt::Allocator *allocator = Aws::Crt::ApiAllocator());

                static IMqttRequestResponseClient *newFrom311(
                    const Aws::Crt::Mqtt::MqttConnection &protocolClient,
                    const RequestResponseClientOptions &options,
                    Aws::Crt::Allocator *allocator = Aws::Crt::ApiAllocator());
            };

        } // namespace RequestResponse
    } // namespace Iot
} // namespace Aws