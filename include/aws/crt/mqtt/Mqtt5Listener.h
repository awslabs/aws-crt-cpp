#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/mqtt/Mqtt5Client.h>
#include <aws/crt/mqtt/Mqtt5Types.h>
#include <aws/mqtt/v5/mqtt5_listener.h>

namespace Aws
{
    namespace Crt
    {
        namespace Mqtt5
        {
            class Mqtt5ListenerOptions;

            /**
             * Type signature of the callback invoked when a PacketPublish message received for Mqtt5ListenerCore.
             *
             * @return true if the message get handled otherwise return false
             */
            using OnListenerPublishReceivedHandler = std::function<bool(const PublishReceivedEventData &)>;

            /**
             * Type signature of the callback invoked when a termination callback invoked.
             */
            using OnListenerTerminationHandler = std::function<void(void *)>;

            /**
             * An MQTT5 listener core. The core is only internally used by Mqtt5Listener to manage the callbacks and
             * underlying c objects.
             *
             * This is a move-only type. Unless otherwise specified, all function arguments need only to live through
             * the duration of the function call.
             */
            class Mqtt5ListenerCore final : public std::enable_shared_from_this<Mqtt5ListenerCore>
            {
              public:
                /**
                 * Factory function for mqtt5 listener
                 *
                 * @param options: Mqtt5ListenerOption
                 * @param client: Mqtt5Client to listen to
                 * @param allocator allocator to use
                 * @return a new mqtt5 listener core
                 */
                static std::shared_ptr<Mqtt5ListenerCore> NewMqtt5ListenerCore(
                    const Mqtt5ListenerOptions &options,
                    const std::shared_ptr<Mqtt5Client> &client,
                    Allocator *allocator = ApiAllocator()) noexcept;

                /**
                 * Get shared pointer of the Mqtt5ListenerCore. Mqtt5ListenerCore is inherited to
                 * enable_shared_from_this to help with memory safety.
                 *
                 * @return shared_ptr for the Mqtt5ListenerCore
                 */
                std::shared_ptr<Mqtt5ListenerCore> getptr() { return shared_from_this(); }

                /**
                 * @return true if the instance is in a valid state, false otherwise.
                 */
                operator bool() const noexcept;

                /**
                 * @return the value of the last aws error encountered by operations on this instance.
                 */
                int LastError() const noexcept;

                /**
                 * Tells the listener to release the native listener and clean up unhandled the resources
                 * and operations before destroying it. You MUST only call this function when you want to
                 * release the listener.
                 * This is "an ugly and unfortunate necessity" before releasing the Mqtt5ListenerCore. And You
                 * MUST call this function to avoid any future memory leaks or dead lock.
                 *
                 */
                void Close() noexcept;

                virtual ~Mqtt5ListenerCore();

              private:
                Mqtt5ListenerCore(
                    const Mqtt5ListenerOptions &options,
                    const std::shared_ptr<Mqtt5Client> &client,
                    Allocator *allocator = ApiAllocator()) noexcept;

                static void s_lifeCycleEventCallback(const aws_mqtt5_client_lifecycle_event *event);

                static bool s_publishReceivedCallback(const aws_mqtt5_packet_publish_view *publish, void *user_data);

                static void s_listenerTerminationCompletion(void *complete_ctx);

                /**
                 * Callback handler triggered when client successfully establishes an MQTT connection
                 */
                OnConnectionSuccessHandler onConnectionSuccess;

                /**
                 * Callback handler triggered when client fails to establish an MQTT connection
                 */
                OnConnectionFailureHandler onConnectionFailure;

                /**
                 * Callback handler triggered when client's current MQTT connection is closed
                 */
                OnDisconnectionHandler onDisconnection;

                /**
                 * Callback handler triggered when client reaches the "Stopped" state
                 */
                OnStoppedHandler onStopped;

                /**
                 * Callback handler triggered when client begins an attempt to connect to the remote endpoint.
                 */
                OnAttemptingConnectHandler onAttemptingConnect;

                /**
                 * Callback handler triggered when an MQTT PUBLISH packet is received by the client
                 */
                OnListenerPublishReceivedHandler onListenerPublishReceived;

                /**
                 * Callback handler triggered when listener terminated
                 */
                OnListenerTerminationHandler onListenerTermination;

                void *termination_userdata;

                /*
                 * Reference to Mqtt5Client. As the listener has a dependency on mqtt5_client, we would like to keep the
                 * Mqtt5Client alive.
                 */
                std::shared_ptr<Mqtt5Client> m_mqtt5Client;

                /*
                 * Self reference to keep self alive until the termination callback is invoked.
                 */
                std::shared_ptr<Mqtt5ListenerCore> m_selfReference;

                /*
                 * The callback flag used to indicate if it is safe to invoke the callbacks
                 */
                enum CallbackFlag
                {
                    INVOKE,
                    IGNORE
                } m_callbackFlag;

                /*
                 * Lock for the callbacks. This is used to protect the callback flag and callbacks.
                 */
                std::recursive_mutex m_callback_lock;

                /**
                 * Underlying c classes for data storage and operational members
                 */
                aws_mqtt5_listener *m_listener;

                Allocator *m_allocator;
            };

            /**
             * An MQTT5 listener.
             *
             * This is a move-only type. Unless otherwise specified, all function arguments need only to live through
             * the duration of the function call.
             */
            class AWS_CRT_CPP_API Mqtt5Listener : public std::enable_shared_from_this<Mqtt5Listener>
            {
              public:
                /**
                 * Factory function for mqtt5 listener
                 *
                 * @param options: Mqtt5ListenerOption
                 * @param client: Mqtt5Client to listen to
                 * @param allocator allocator to use
                 * @return a new mqtt5 listener
                 */
                static std::shared_ptr<Mqtt5Listener> NewMqtt5Listener(
                    const Mqtt5ListenerOptions &options,
                    const std::shared_ptr<Mqtt5Client> &client,
                    Allocator *allocator = ApiAllocator()) noexcept;

                /**
                 * Get shared pointer of the Mqtt5ListenerCore. Mqtt5ListenerCore is inherited to
                 * enable_shared_from_this to help with memory safety.
                 *
                 * @return shared_ptr for the Mqtt5ListenerCore
                 */
                std::shared_ptr<Mqtt5Listener> getptr() { return shared_from_this(); }

                /**
                 * @return true if the instance is in a valid state, false otherwise.
                 */
                operator bool() const noexcept { return m_listener_core != nullptr; };

                /**
                 * @return the value of the last aws error encountered by operations on this instance.
                 */
                int LastError() const noexcept { return aws_last_error(); };

                virtual ~Mqtt5Listener();

              private:
                Mqtt5Listener(
                    const Mqtt5ListenerOptions &options,
                    const std::shared_ptr<Mqtt5Client> &client,
                    Allocator *allocator) noexcept;

                /*
                 * Reference to listener core.
                 */
                std::shared_ptr<Mqtt5ListenerCore> m_listener_core;
                Allocator *m_allocator;
            };

            /**
             * Configuration interface for mqtt5 listener
             */
            class AWS_CRT_CPP_API Mqtt5ListenerOptions final
            {

                friend class Mqtt5ListenerCore;

              public:
                /**
                 * Default constructor of Mqtt5ListenerOption
                 */
                Mqtt5ListenerOptions() noexcept;

                /**
                 * Sets callback triggered when Listener successfully establishes an MQTT connection
                 *
                 * @param callback
                 *
                 * @return this option object
                 */
                Mqtt5ListenerOptions &WithListenerConnectionSuccessCallback(
                    OnConnectionSuccessHandler callback) noexcept;

                /**
                 * Sets callback triggered when Listener fails to establish an MQTT connection
                 *
                 * @param callback
                 *
                 * @return this option object
                 */
                Mqtt5ListenerOptions &WithListenerConnectionFailureCallback(
                    OnConnectionFailureHandler callback) noexcept;

                /**
                 * Sets callback triggered when Listener's current MQTT connection is closed
                 *
                 * @param callback
                 *
                 * @return this option object
                 */
                Mqtt5ListenerOptions &WithListenerDisconnectionCallback(OnDisconnectionHandler callback) noexcept;

                /**
                 * Sets callback triggered when Listener reaches the "Stopped" state
                 *
                 * @param callback
                 *
                 * @return this option object
                 */
                Mqtt5ListenerOptions &WithListenerStoppedCallback(OnStoppedHandler callback) noexcept;

                /**
                 * Sets callback triggered when Listener begins an attempt to connect to the remote endpoint.
                 *
                 * @param callback
                 *
                 * @return this option object
                 */
                Mqtt5ListenerOptions &WithListenerAttemptingConnectCallback(
                    OnAttemptingConnectHandler callback) noexcept;

                /**
                 * Sets callback triggered when a PUBLISH packet is received by the Listener
                 *
                 * @param callback
                 *
                 * @return this option object
                 */
                Mqtt5ListenerOptions &WithListenerPublishReceivedCallback(
                    OnListenerPublishReceivedHandler callback) noexcept;

                /**
                 * Sets callback triggered when the termination process ends
                 *
                 * @param callback
                 * @param user_data
                 *
                 * @return this option object
                 */
                Mqtt5ListenerOptions &WithListenerTerminationCallback(
                    OnListenerTerminationHandler callback,
                    void *user_data) noexcept;

                virtual ~Mqtt5ListenerOptions();
                Mqtt5ListenerOptions(const Mqtt5ListenerOptions &) = delete;
                Mqtt5ListenerOptions(Mqtt5ListenerOptions &&) = delete;
                Mqtt5ListenerOptions &operator=(const Mqtt5ListenerOptions &) = delete;
                Mqtt5ListenerOptions &operator=(Mqtt5ListenerOptions &&) = delete;

              private:
                /**
                 * Callback handler triggered when Listener successfully establishes an MQTT connection
                 */
                OnConnectionSuccessHandler onConnectionSuccess;

                /**
                 * Callback handler triggered when Listener fails to establish an MQTT connection
                 */
                OnConnectionFailureHandler onConnectionFailure;

                /**
                 * Callback handler triggered when Listener's current MQTT connection is closed
                 */
                OnDisconnectionHandler onDisconnection;

                /**
                 * Callback handler triggered when client reaches the "Stopped" state
                 */
                OnStoppedHandler onStopped;

                /**
                 * Callback handler triggered when client begins an attempt to connect to the remote endpoint.
                 */
                OnAttemptingConnectHandler onAttemptingConnect;

                /**
                 * Callback handler triggered when the listener terminated
                 */
                OnListenerTerminationHandler onListenerTermination;
                void *termination_userdata;

                /**
                 * Callback handler triggered when an MQTT PUBLISH packet is received by the client
                 *
                 * @param PublishPacket: received Publish Packet
                 */
                OnListenerPublishReceivedHandler onListenerPublishReceived;
            };

        } // namespace Mqtt5
    }     // namespace Crt
} // namespace Aws
