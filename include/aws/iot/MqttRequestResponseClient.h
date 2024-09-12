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

namespace Aws {

namespace Crt {
namespace Mqtt {
    class MqttConnection;
}

namespace Mqtt5 {
    class Mqtt5Client;
}
}

namespace Iot {
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
     * to its topic (filter).  This is always due to user error (bad topic filter or IoT Core permission policy).
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

    struct AWS_CRT_CPP_API IncomingPublishEvent {
        Aws::Crt::Vector<uint8_t> payload;
    };

    using IncomingPublishEventHandler = std::function<void(IncomingPublishEvent &&)>;

    /**
 * A response path is a pair of values - MQTT topic and a JSON path - that describe how a response to
 * an MQTT-based request may arrive.  For a given request type, there may be multiple response paths and each
 * one is associated with a separate JSON schema for the response body.
     */
    struct AWS_CRT_CPP_API ResponsePath {

        /**
     * MQTT topic that a response may arrive on.
         */
        Aws::Crt::String topic;

        /**
     * JSON path for finding correlation tokens within payloads that arrive on this path's topic.
         */
        Aws::Crt::Optional<Aws::Crt::String> correlationTokenJsonPath;
    };

    /**
 * Configuration options for an MQTT-based request-response operation.
     */
    struct AWS_CRT_CPP_API RequestResponseOperationOptions {

        /**
     * Set of topic filters that should be subscribed to in order to cover all possible response paths.  Sometimes
     * using wildcards can cut down on the subscriptions needed; other times that isn't valid.
         */
        Aws::Crt::Vector<Aws::Crt::String> subscriptionTopicFilters;

        /**
     * Set of all possible response paths associated with this request type.
         */
        Aws::Crt::Vector<ResponsePath> responsePaths;

        /**
     * Topic to publish the request to once response subscriptions have been established.
         */
        Aws::Crt::String publishTopic;

        /**
     * Payload to publish to 'publishTopic' in order to initiate the request
         */
        Aws::Crt::Vector<uint8_t> payload;

        /**
     * Correlation token embedded in the request that must be found in a response message.  This can be null
     * to support certain services which don't use correlation tokens.  In that case, the client
     * only allows one token-less request at a time.
         */
        Aws::Crt::Optional<Aws::Crt::String> correlationToken;
    };

    /**
 * Configuration options for an MQTT-based streaming operation.
     */
    struct AWS_CRT_CPP_API StreamingOperationOptions {

        /**
     * Topic filter that the streaming operation should listen on
         */
        Aws::Crt::String subscriptionTopicFilter;
    };

    /**
 * Encapsulates a response to an AWS IoT Core MQTT-based service request
     */
    struct AWS_CRT_CPP_API Response {

        /**
     * MQTT Topic that the response was received on.  Different topics map to different types within the
     * service model, so we need this value in order to know what to deserialize the payload into.
         */
        struct aws_byte_cursor topic;

        /**
     * Payload of the response that correlates to a submitted request.
         */
        struct aws_byte_cursor payload;
    };

    template <typename R, typename E> struct Result {
      public:

        Result() = delete;
        Result(const Result &result) = default;
        Result(Result &&result) = default;

        Result(const R &response) :
          rawResult(response)
        {}

        Result(R &&response) :
              rawResult(std::move(response))
        {}

        Result(const E &error) :
              rawResult(error)
        {}

        Result(E &&error) :
              rawResult(std::move(error))
        {}

        ~Result() = default;

        Result &operator=(const Result &result) = default;
        Result &operator=(Result &&result) = default;

        Result &operator=(const R &response) {
            this->rawResult = response;

            return *this;
        }

        Result &operator=(R &&response) {
            this->rawResult = std::move(response);

            return *this;
        }

        Result &operator=(const E &error) {
            this->rawResult = error;
        }

        Result &operator=(E &&error) {
            this->rawResult = std::move(error);

            return *this;
        }

        bool isSuccess() const {
            return rawResult.holds_alternative<R>();
        }

        const R &getResponse() const {
            AWS_FATAL_ASSERT(isSuccess());

            return rawResult.get<Response>();
        }

        const E &getError() const {
            AWS_FATAL_ASSERT(!isSuccess());

            return rawResult.get<int>();
        }

      private:

        Aws::Crt::Variant<R, E> rawResult;
    };

    using UnmodeledResult = Result<Response, int>;

    using UnmodeledResultHandler = std::function<void(UnmodeledResult &&)>;

    /**
 * MQTT-based request-response client configuration options
     */
    struct AWS_CRT_CPP_API RequestResponseClientOptions {

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



    class AWS_CRT_CPP_API MqttRequestResponseClient {
        public:

            virtual ~MqttRequestResponseClient();

            static MqttRequestResponseClient *newFrom5(const Aws::Crt::Mqtt5::Mqtt5Client &protocolClient, RequestResponseClientOptions &&options, Aws::Crt::Allocator *allocator = Aws::Crt::ApiAllocator());

            static MqttRequestResponseClient *newFrom311(const Aws::Crt::Mqtt::MqttConnection &protocolClient, RequestResponseClientOptions &&options, Aws::Crt::Allocator *allocator = Aws::Crt::ApiAllocator());

            int submitRequest(const RequestResponseOperationOptions &requestOptions, UnmodeledResultHandler &&resultHandler);

        private:

          MqttRequestResponseClient(Aws::Crt::Allocator *allocator, std::shared_ptr<MqttRequestResponseClientImpl> impl);

          Aws::Crt::Allocator *m_allocator;

            std::shared_ptr<MqttRequestResponseClientImpl> m_impl;
    };


} // RequestResponse
} // Iot
} // Aws