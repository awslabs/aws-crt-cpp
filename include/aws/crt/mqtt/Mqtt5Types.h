#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/mqtt/v5/mqtt5_client.h>
#include <aws/mqtt/v5/mqtt5_types.h>

namespace Aws
{
    namespace Crt
    {
        namespace Mqtt5
        {
            /**
             * MQTT message delivery quality of service.
             *
             * Enum values match [MQTT5
             * spec](https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_Toc3901234) encoding values.
             */
            enum QOS
            {
                /** https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_Toc3901235 */
                AWS_MQTT5_QOS_AT_MOST_ONCE = aws_mqtt5_qos::AWS_MQTT5_QOS_AT_LEAST_ONCE,
                /** https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_Toc3901236 */
                AWS_MQTT5_QOS_AT_LEAST_ONCE = aws_mqtt5_qos::AWS_MQTT5_QOS_AT_LEAST_ONCE,
                /** https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_Toc3901237 */
                AWS_MQTT5_QOS_EXACTLY_ONCE = aws_mqtt5_qos::AWS_MQTT5_QOS_EXACTLY_ONCE,
            };

            /**
             * Server return code for connect attempts.
             *
             * Enum values match [MQTT5
             * spec](https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_Toc3901079) encoding values.
             */
            enum ConnectReasonCode
            {
                /** 0 */
                AWS_MQTT5_CRC_SUCCESS = aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_SUCCESS,
                /** 128 */
                AWS_MQTT5_CRC_UNSPECIFIED_ERROR = aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_UNSPECIFIED_ERROR,
                /** 129 */
                AWS_MQTT5_CRC_MALFORMED_PACKET = aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_MALFORMED_PACKET,
                /** 130 */
                AWS_MQTT5_CRC_PROTOCOL_ERROR = aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_MALFORMED_PACKET,
                /** 131 */
                AWS_MQTT5_CRC_IMPLEMENTATION_SPECIFIC_ERROR =
                    aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_IMPLEMENTATION_SPECIFIC_ERROR,
                /** 132 */
                AWS_MQTT5_CRC_UNSUPPORTED_PROTOCOL_VERSION =
                    aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_UNSUPPORTED_PROTOCOL_VERSION,
                /** 133 */
                AWS_MQTT5_CRC_CLIENT_IDENTIFIER_NOT_VALID =
                    aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_CLIENT_IDENTIFIER_NOT_VALID,
                /** 134 */
                AWS_MQTT5_CRC_BAD_USERNAME_OR_PASSWORD =
                    aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_BAD_USERNAME_OR_PASSWORD,
                /** 135 */
                AWS_MQTT5_CRC_NOT_AUTHORIZED = aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_NOT_AUTHORIZED,
                /** 136 */
                AWS_MQTT5_CRC_SERVER_UNAVAILABLE = aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_SERVER_UNAVAILABLE,
                /** 137 */
                AWS_MQTT5_CRC_SERVER_BUSY = aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_SERVER_BUSY,
                /** 138 */
                AWS_MQTT5_CRC_BANNED = aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_BANNED,
                /** 140 */
                AWS_MQTT5_CRC_BAD_AUTHENTICATION_METHOD =
                    aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_BAD_AUTHENTICATION_METHOD,
                /** 144 */
                AWS_MQTT5_CRC_TOPIC_NAME_INVALID = aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_TOPIC_NAME_INVALID,
                /** 149 */
                AWS_MQTT5_CRC_PACKET_TOO_LARGE = aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_PACKET_TOO_LARGE,
                /** 151 */
                AWS_MQTT5_CRC_QUOTA_EXCEEDED = aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_QUOTA_EXCEEDED,
                /** 153 */
                AWS_MQTT5_CRC_PAYLOAD_FORMAT_INVALID =
                    aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_PAYLOAD_FORMAT_INVALID,
                /** 154 */
                AWS_MQTT5_CRC_RETAIN_NOT_SUPPORTED = aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_RETAIN_NOT_SUPPORTED,
                /** 155 */
                AWS_MQTT5_CRC_QOS_NOT_SUPPORTED = aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_QOS_NOT_SUPPORTED,
                /** 156 */
                AWS_MQTT5_CRC_USE_ANOTHER_SERVER = aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_USE_ANOTHER_SERVER,
                /** 157 */
                AWS_MQTT5_CRC_SERVER_MOVED = aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_SERVER_MOVED,
                /** 159 */
                AWS_MQTT5_CRC_CONNECTION_RATE_EXCEEDED =
                    aws_mqtt5_connect_reason_code::AWS_MQTT5_CRC_CONNECTION_RATE_EXCEEDED,

            };

            /**
             * Reason code inside DISCONNECT packets.  Helps determine why a connection was terminated.
             *
             * Enum values match [MQTT5
             * spec](https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_Toc3901208) encoding values.
             */
            enum DisconnectReasonCode
            {
                /** 0 */
                AWS_MQTT5_DRC_NORMAL_DISCONNECTION =
                    aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_NORMAL_DISCONNECTION,
                /** 4 */
                AWS_MQTT5_DRC_DISCONNECT_WITH_WILL_MESSAGE =
                    aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_DISCONNECT_WITH_WILL_MESSAGE,
                /** 128 */
                AWS_MQTT5_DRC_UNSPECIFIED_ERROR = aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_UNSPECIFIED_ERROR,
                /** 129 */
                AWS_MQTT5_DRC_MALFORMED_PACKET = aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_MALFORMED_PACKET,
                /** 130 */
                AWS_MQTT5_DRC_PROTOCOL_ERROR = aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_PROTOCOL_ERROR,
                /** 131 */
                AWS_MQTT5_DRC_IMPLEMENTATION_SPECIFIC_ERROR =
                    aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_IMPLEMENTATION_SPECIFIC_ERROR,
                /** 135 */
                AWS_MQTT5_DRC_NOT_AUTHORIZED = aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_NOT_AUTHORIZED,
                /** 137 */
                AWS_MQTT5_DRC_SERVER_BUSY = aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_SERVER_BUSY,
                /** 139 */
                AWS_MQTT5_DRC_SERVER_SHUTTING_DOWN =
                    aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_SERVER_SHUTTING_DOWN,
                /** 141 */
                AWS_MQTT5_DRC_KEEP_ALIVE_TIMEOUT = aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_KEEP_ALIVE_TIMEOUT,
                /** 142 */
                AWS_MQTT5_DRC_SESSION_TAKEN_OVER = aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_SESSION_TAKEN_OVER,
                /** 143 */
                AWS_MQTT5_DRC_TOPIC_FILTER_INVALID =
                    aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_TOPIC_FILTER_INVALID,
                /** 144 */
                AWS_MQTT5_DRC_TOPIC_NAME_INVALID = aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_TOPIC_NAME_INVALID,
                /** 147 */
                AWS_MQTT5_DRC_RECEIVE_MAXIMUM_EXCEEDED =
                    aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_RECEIVE_MAXIMUM_EXCEEDED,
                /** 148 */
                AWS_MQTT5_DRC_TOPIC_ALIAS_INVALID = aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_TOPIC_ALIAS_INVALID,
                /** 149 */
                AWS_MQTT5_DRC_PACKET_TOO_LARGE = aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_PACKET_TOO_LARGE,
                /** 150 */
                AWS_MQTT5_DRC_MESSAGE_RATE_TOO_HIGH =
                    aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_MESSAGE_RATE_TOO_HIGH,
                /** 151 */
                AWS_MQTT5_DRC_QUOTA_EXCEEDED = aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_QUOTA_EXCEEDED,
                /** 152 */
                AWS_MQTT5_DRC_ADMINISTRATIVE_ACTION =
                    aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_ADMINISTRATIVE_ACTION,
                /** 153 */
                AWS_MQTT5_DRC_PAYLOAD_FORMAT_INVALID =
                    aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_PAYLOAD_FORMAT_INVALID,
                /** 154 */
                AWS_MQTT5_DRC_RETAIN_NOT_SUPPORTED =
                    aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_RETAIN_NOT_SUPPORTED,
                /** 155 */
                AWS_MQTT5_DRC_QOS_NOT_SUPPORTED = aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_QOS_NOT_SUPPORTED,
                /** 156 */
                AWS_MQTT5_DRC_USE_ANOTHER_SERVER = aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_USE_ANOTHER_SERVER,
                /** 157 */
                AWS_MQTT5_DRC_SERVER_MOVED = aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_SERVER_MOVED,
                /** 158 */
                AWS_MQTT5_DRC_SHARED_SUBSCRIPTIONS_NOT_SUPPORTED =
                    aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_SHARED_SUBSCRIPTIONS_NOT_SUPPORTED,
                /** 159 */
                AWS_MQTT5_DRC_CONNECTION_RATE_EXCEEDED =
                    aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_CONNECTION_RATE_EXCEEDED,
                /** 160 */
                AWS_MQTT5_DRC_MAXIMUM_CONNECT_TIME =
                    aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_MAXIMUM_CONNECT_TIME,
                /** 161 */
                AWS_MQTT5_DRC_SUBSCRIPTION_IDENTIFIERS_NOT_SUPPORTED =
                    aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_SUBSCRIPTION_IDENTIFIERS_NOT_SUPPORTED,
                /** 162 */
                AWS_MQTT5_DRC_WILDCARD_SUBSCRIPTIONS_NOT_SUPPORTED =
                    aws_mqtt5_disconnect_reason_code::AWS_MQTT5_DRC_WILDCARD_SUBSCRIPTIONS_NOT_SUPPORTED,
            };

            /**
             * Reason code inside PUBACK packets
             *
             * Data model of an [MQTT5
             * PUBACK](https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_Toc3901121) packet
             */
            enum PubAckReasonCode
            {
                /** 0 */
                AWS_MQTT5_PARC_SUCCESS = aws_mqtt5_puback_reason_code::AWS_MQTT5_PARC_SUCCESS,
                /** 16 */
                AWS_MQTT5_PARC_NO_MATCHING_SUBSCRIBERS =
                    aws_mqtt5_puback_reason_code::AWS_MQTT5_PARC_NO_MATCHING_SUBSCRIBERS,
                /** 128 */
                AWS_MQTT5_PARC_UNSPECIFIED_ERROR = aws_mqtt5_puback_reason_code::AWS_MQTT5_PARC_UNSPECIFIED_ERROR,
                /** 131 */
                AWS_MQTT5_PARC_IMPLEMENTATION_SPECIFIC_ERROR =
                    aws_mqtt5_puback_reason_code::AWS_MQTT5_PARC_IMPLEMENTATION_SPECIFIC_ERROR,
                /** 135 */
                AWS_MQTT5_PARC_NOT_AUTHORIZED = aws_mqtt5_puback_reason_code::AWS_MQTT5_PARC_NOT_AUTHORIZED,
                /** 144 */
                AWS_MQTT5_PARC_TOPIC_NAME_INVALID = aws_mqtt5_puback_reason_code::AWS_MQTT5_PARC_TOPIC_NAME_INVALID,
                /** 145 */
                AWS_MQTT5_PARC_PACKET_IDENTIFIER_IN_USE =
                    aws_mqtt5_puback_reason_code::AWS_MQTT5_PARC_PACKET_IDENTIFIER_IN_USE,
                /** 151 */
                AWS_MQTT5_PARC_QUOTA_EXCEEDED = aws_mqtt5_puback_reason_code::AWS_MQTT5_PARC_QUOTA_EXCEEDED,
                /** 153 */
                AWS_MQTT5_PARC_PAYLOAD_FORMAT_INVALID =
                    aws_mqtt5_puback_reason_code::AWS_MQTT5_PARC_PAYLOAD_FORMAT_INVALID,

            };

            /**
             * Reason code inside PUBACK packets that indicates the result of the associated PUBLISH request.
             *
             * Enum values match [MQTT5
             * spec](https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_Toc3901124) encoding values.
             */
            enum SubAckReasonCode
            {
                /** 0 */
                AWS_MQTT5_SARC_GRANTED_QOS_0 = aws_mqtt5_suback_reason_code::AWS_MQTT5_SARC_GRANTED_QOS_0,
                /** 1 */
                AWS_MQTT5_SARC_GRANTED_QOS_1 = aws_mqtt5_suback_reason_code::AWS_MQTT5_SARC_GRANTED_QOS_1,
                /** 2 */
                AWS_MQTT5_SARC_GRANTED_QOS_2 = aws_mqtt5_suback_reason_code::AWS_MQTT5_SARC_GRANTED_QOS_2,
                /** 128 */
                AWS_MQTT5_SARC_UNSPECIFIED_ERROR = aws_mqtt5_suback_reason_code::AWS_MQTT5_SARC_UNSPECIFIED_ERROR,
                /** 131 */
                AWS_MQTT5_SARC_IMPLEMENTATION_SPECIFIC_ERROR =
                    aws_mqtt5_suback_reason_code::AWS_MQTT5_SARC_IMPLEMENTATION_SPECIFIC_ERROR,
                /** 135 */
                AWS_MQTT5_SARC_NOT_AUTHORIZED = aws_mqtt5_suback_reason_code::AWS_MQTT5_SARC_NOT_AUTHORIZED,
                /** 143 */
                AWS_MQTT5_SARC_TOPIC_FILTER_INVALID = aws_mqtt5_suback_reason_code::AWS_MQTT5_SARC_TOPIC_FILTER_INVALID,
                /** 145 */
                AWS_MQTT5_SARC_PACKET_IDENTIFIER_IN_USE =
                    aws_mqtt5_suback_reason_code::AWS_MQTT5_SARC_PACKET_IDENTIFIER_IN_USE,
                /** 151 */
                AWS_MQTT5_SARC_QUOTA_EXCEEDED = aws_mqtt5_suback_reason_code::AWS_MQTT5_SARC_QUOTA_EXCEEDED,
                /** 158 */
                AWS_MQTT5_SARC_SHARED_SUBSCRIPTIONS_NOT_SUPPORTED =
                    aws_mqtt5_suback_reason_code::AWS_MQTT5_SARC_SHARED_SUBSCRIPTIONS_NOT_SUPPORTED,
                /** 161 */
                AWS_MQTT5_SARC_SUBSCRIPTION_IDENTIFIERS_NOT_SUPPORTED =
                    aws_mqtt5_suback_reason_code::AWS_MQTT5_SARC_SUBSCRIPTION_IDENTIFIERS_NOT_SUPPORTED,
                /** 162 */
                AWS_MQTT5_SARC_WILDCARD_SUBSCRIPTIONS_NOT_SUPPORTED =
                    aws_mqtt5_suback_reason_code::AWS_MQTT5_SARC_WILDCARD_SUBSCRIPTIONS_NOT_SUPPORTED,
            };

            /**
             * Reason codes inside UNSUBACK packet payloads that specify the results for each topic filter in the
             * associated UNSUBSCRIBE packet.
             *
             * Enum values match [MQTT5
             * spec](https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_Toc3901194) encoding values.
             */
            enum UnSubAckReasonCode
            {
                /** 0 */
                AWS_MQTT5_UARC_SUCCESS = aws_mqtt5_unsuback_reason_code::AWS_MQTT5_UARC_SUCCESS,
                /** 17 */
                AWS_MQTT5_UARC_NO_SUBSCRIPTION_EXISTED =
                    aws_mqtt5_unsuback_reason_code::AWS_MQTT5_UARC_NO_SUBSCRIPTION_EXISTED,
                /** 128 */
                AWS_MQTT5_UARC_UNSPECIFIED_ERROR = aws_mqtt5_unsuback_reason_code::AWS_MQTT5_UARC_UNSPECIFIED_ERROR,
                /** 131 */
                AWS_MQTT5_UARC_IMPLEMENTATION_SPECIFIC_ERROR =
                    aws_mqtt5_unsuback_reason_code::AWS_MQTT5_UARC_IMPLEMENTATION_SPECIFIC_ERROR,
                /** 135 */
                AWS_MQTT5_UARC_NOT_AUTHORIZED = aws_mqtt5_unsuback_reason_code::AWS_MQTT5_UARC_NOT_AUTHORIZED,
                /** 143 */
                AWS_MQTT5_UARC_TOPIC_FILTER_INVALID =
                    aws_mqtt5_unsuback_reason_code::AWS_MQTT5_UARC_TOPIC_FILTER_INVALID,
                /** 145 */
                AWS_MQTT5_UARC_PACKET_IDENTIFIER_IN_USE =
                    aws_mqtt5_unsuback_reason_code::AWS_MQTT5_UARC_PACKET_IDENTIFIER_IN_USE,

            };

            /**
             * Controls how the MQTT5 client should behave with respect to MQTT sessions.
             */
            enum ClientSessionBehaviorType
            {
                /**
                 * Maps to AWS_MQTT5_CSBT_CLEAN
                 */
                AWS_MQTT5_CSBT_DEFAULT = aws_mqtt5_client_session_behavior_type::AWS_MQTT5_CSBT_DEFAULT,

                /**
                 * Always join a new, clean session
                 */
                AWS_MQTT5_CSBT_CLEAN = aws_mqtt5_client_session_behavior_type::AWS_MQTT5_CSBT_CLEAN,

                /**
                 * Always attempt to rejoin an existing session after an initial connection success.
                 */
                AWS_MQTT5_CSBT_REJOIN_POST_SUCCESS =
                    aws_mqtt5_client_session_behavior_type::AWS_MQTT5_CSBT_REJOIN_POST_SUCCESS,

                /**
                 * Always attempt to rejoin an existing session.  Since the client does not support durable session
                 * persistence, this option is not guaranteed to be spec compliant because any unacknowledged qos1
                 * publishes (which are part of the client session state) will not be present on the initial connection.
                 * Until we support durable session resumption, this option is technically spec-breaking, but useful.
                 */
                AWS_MQTT5_CSBT_REJOIN_ALWAYS = aws_mqtt5_client_session_behavior_type::AWS_MQTT5_CSBT_REJOIN_ALWAYS,
            };

            /**
             * Additional controls for client behavior with respect to operation validation and flow control; these
             * checks go beyond the MQTT5 spec to respect limits of specific MQTT brokers.
             */
            enum ClientExtendedValidationAndFlowControl
            {
                /**
                 * Do not do any additional validation or flow control outside of the MQTT5 spec
                 */
                AWS_MQTT5_EVAFCO_NONE = aws_mqtt5_extended_validation_and_flow_control_options::AWS_MQTT5_EVAFCO_NONE,

                /**
                 * Apply additional client-side operational flow control that respects the
                 * default AWS IoT Core limits.
                 *
                 * Applies the following flow control:
                 *  (1) Outbound throughput throttled to 512KB/s
                 *  (2) Outbound publish TPS throttled to 100
                 */
                AWS_MQTT5_EVAFCO_AWS_IOT_CORE_DEFAULTS =
                    aws_mqtt5_extended_validation_and_flow_control_options::AWS_MQTT5_EVAFCO_AWS_IOT_CORE_DEFAULTS,
            };

            /**
             * Controls how disconnects affect the queued and in-progress operations tracked by the client.  Also
             * controls how operations are handled while the client is not connected.  In particular, if the client is
             * not connected, then any operation that would be failed on disconnect (according to these rules) will be
             * rejected.
             */
            enum ClientOperationQueueBehaviorType
            {
                /*
                 * Maps to AWS_MQTT5_COQBT_FAIL_QOS0_PUBLISH_ON_DISCONNECT
                 */
                AWS_MQTT5_COQBT_DEFAULT = aws_mqtt5_client_operation_queue_behavior_type::AWS_MQTT5_COQBT_DEFAULT,

                /*
                 * Requeues QoS 1+ publishes on disconnect; unacked publishes go to the front, unprocessed publishes
                 * stay in place.  All other operations (QoS 0 publishes, subscribe, unsubscribe) are failed.
                 */
                AWS_MQTT5_COQBT_FAIL_NON_QOS1_PUBLISH_ON_DISCONNECT =
                    aws_mqtt5_client_operation_queue_behavior_type::AWS_MQTT5_COQBT_FAIL_NON_QOS1_PUBLISH_ON_DISCONNECT,

                /*
                 * Qos 0 publishes that are not complete at the time of disconnection are failed.  Unacked QoS 1+
                 * publishes are requeued at the head of the line for immediate retransmission on a session resumption.
                 * All other operations are requeued in original order behind any retransmissions.
                 */
                AWS_MQTT5_COQBT_FAIL_QOS0_PUBLISH_ON_DISCONNECT =
                    aws_mqtt5_client_operation_queue_behavior_type::AWS_MQTT5_COQBT_FAIL_QOS0_PUBLISH_ON_DISCONNECT,

                /*
                 * All operations that are not complete at the time of disconnection are failed, except those operations
                 * that the mqtt 5 spec requires to be retransmitted (unacked qos1+ publishes).
                 */
                AWS_MQTT5_COQBT_FAIL_ALL_ON_DISCONNECT =
                    aws_mqtt5_client_operation_queue_behavior_type::AWS_MQTT5_COQBT_FAIL_ALL_ON_DISCONNECT,
            };

            /**
             * Controls how the reconnect delay is modified in order to smooth out the distribution of reconnection
             * attempt timepoints for a large set of reconnecting clients.
             *
             * See [Exponential Backoff and
             * Jitter](https://aws.amazon.com/blogs/architecture/exponential-backoff-and-jitter/)
             */
            enum ExponentialBackoffJitterMode
            {
                /* uses AWS_EXPONENTIAL_BACKOFF_JITTER_FULL */
                AWS_EXPONENTIAL_BACKOFF_JITTER_DEFAULT =
                    aws_exponential_backoff_jitter_mode::AWS_EXPONENTIAL_BACKOFF_JITTER_DEFAULT,
                AWS_EXPONENTIAL_BACKOFF_JITTER_NONE =
                    aws_exponential_backoff_jitter_mode::AWS_EXPONENTIAL_BACKOFF_JITTER_NONE,
                AWS_EXPONENTIAL_BACKOFF_JITTER_FULL =
                    aws_exponential_backoff_jitter_mode::AWS_EXPONENTIAL_BACKOFF_JITTER_FULL,
                AWS_EXPONENTIAL_BACKOFF_JITTER_DECORRELATED =
                    aws_exponential_backoff_jitter_mode::AWS_EXPONENTIAL_BACKOFF_JITTER_DECORRELATED,
            };

            /** @deprecated JitterMode is deprecated, please use  Aws::Crt::Mqtt5::ExponentialBackoffJitterMode */
            using JitterMode = ExponentialBackoffJitterMode;

            /**
             * Optional property describing a PUBLISH payload's format.
             *
             * Enum values match [MQTT5
             * spec](https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_Toc3901111) encoding values.
             */
            enum PayloadFormatIndicator
            {
                /** 0 */
                AWS_MQTT5_PFI_BYTES = aws_mqtt5_payload_format_indicator::AWS_MQTT5_PFI_BYTES,
                /** 1 */
                AWS_MQTT5_PFI_UTF8 = aws_mqtt5_payload_format_indicator::AWS_MQTT5_PFI_UTF8,
            };

            /**
             * Configures how retained messages should be handled when subscribing with a topic filter that matches
             * topics with associated retained messages.
             *
             * Enum values match [MQTT5
             * spec](https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_Toc3901169) encoding values.
             */
            enum RetainHandlingType
            {
                /**
                 * Server should send all retained messages on topics that match the subscription's filter.
                 */
                AWS_MQTT5_RHT_SEND_ON_SUBSCRIBE = aws_mqtt5_retain_handling_type::AWS_MQTT5_RHT_SEND_ON_SUBSCRIBE,

                /**
                 * Server should send all retained messages on topics that match the subscription's filter, where this
                 * is the first (relative to connection) subscription filter that matches the topic with a retained
                 * message.
                 */
                AWS_MQTT5_RHT_SEND_ON_SUBSCRIBE_IF_NEW =
                    aws_mqtt5_retain_handling_type::AWS_MQTT5_RHT_SEND_ON_SUBSCRIBE_IF_NEW,

                /**
                 * Subscribe must not trigger any retained message publishes from the server.
                 */
                AWS_MQTT5_RHT_DONT_SEND = aws_mqtt5_retain_handling_type::AWS_MQTT5_RHT_DONT_SEND,
            };

            /**
             * Type of mqtt packet.
             * Enum values match mqtt spec encoding values.
             *
             * https://docs.oasis-open.org/mqtt/mqtt/v5.0/os/mqtt-v5.0-os.html#_Toc3901022
             */
            enum PacketType
            {
                /* internal indicator that the associated packet is null */
                AWS_MQTT5_PT_NONE = aws_mqtt5_packet_type::AWS_MQTT5_PT_NONE,
                AWS_MQTT5_PT_RESERVED = aws_mqtt5_packet_type::AWS_MQTT5_PT_RESERVED,
                AWS_MQTT5_PT_CONNECT = aws_mqtt5_packet_type::AWS_MQTT5_PT_CONNECT,
                AWS_MQTT5_PT_CONNACK = aws_mqtt5_packet_type::AWS_MQTT5_PT_CONNACK,
                AWS_MQTT5_PT_PUBLISH = aws_mqtt5_packet_type::AWS_MQTT5_PT_PUBLISH,
                AWS_MQTT5_PT_PUBACK = aws_mqtt5_packet_type::AWS_MQTT5_PT_PUBACK,
                AWS_MQTT5_PT_PUBREC = aws_mqtt5_packet_type::AWS_MQTT5_PT_PUBREC,
                AWS_MQTT5_PT_PUBREL = aws_mqtt5_packet_type::AWS_MQTT5_PT_PUBREL,
                AWS_MQTT5_PT_PUBCOMP = aws_mqtt5_packet_type::AWS_MQTT5_PT_PUBCOMP,
                AWS_MQTT5_PT_SUBSCRIBE = aws_mqtt5_packet_type::AWS_MQTT5_PT_SUBSCRIBE,
                AWS_MQTT5_PT_SUBACK = aws_mqtt5_packet_type::AWS_MQTT5_PT_SUBACK,
                AWS_MQTT5_PT_UNSUBSCRIBE = aws_mqtt5_packet_type::AWS_MQTT5_PT_UNSUBSCRIBE,
                AWS_MQTT5_PT_UNSUBACK = aws_mqtt5_packet_type::AWS_MQTT5_PT_UNSUBACK,
                AWS_MQTT5_PT_PINGREQ = aws_mqtt5_packet_type::AWS_MQTT5_PT_PINGREQ,
                AWS_MQTT5_PT_PINGRESP = aws_mqtt5_packet_type::AWS_MQTT5_PT_PINGRESP,
                AWS_MQTT5_PT_DISCONNECT = aws_mqtt5_packet_type::AWS_MQTT5_PT_DISCONNECT,
                AWS_MQTT5_PT_AUTH = aws_mqtt5_packet_type::AWS_MQTT5_PT_AUTH,
            };

        } // namespace Mqtt5

    } // namespace Crt
} // namespace Aws
