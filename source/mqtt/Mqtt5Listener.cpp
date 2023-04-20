#include <aws/crt/mqtt/Mqtt5ClientCore.h>
#include <aws/crt/mqtt/Mqtt5Listener.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/crt/mqtt/Mqtt5Types.h>

namespace Aws
{
    namespace Crt
    {
        namespace Mqtt5
        {

            /**
             * An MQTT5 listener core. The core is only used internally by Mqtt5Listener to manage the callbacks and
             * underlying c objects.
             *
             * This is a move-only type. Unless otherwise specified, all function arguments only need to live through
             * the duration of the function call.
             */
            class Mqtt5ListenerCore
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
                 * @return true if the instance is in a valid state, false otherwise.
                 */
                operator bool() const noexcept;

                /**
                 * @return the value of the last aws error encountered by operations on this instance.
                 */
                int LastError() const noexcept;

                /**
                 * Tells the listener to release the native listener and clean up unhandled resources and operations
                 * before destroying it. You MUST only call this function when you want to release the listener. This is
                 * "an ugly and unfortunate necessity" before releasing the Mqtt5ListenerCore. You MUST call this
                 * function to avoid memory leaks and/or dead locks.
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

            std::shared_ptr<Mqtt5ListenerCore> Aws::Crt::Mqtt5::Mqtt5ListenerCore::NewMqtt5ListenerCore(
                const Mqtt5ListenerOptions &options,
                const std::shared_ptr<Mqtt5Client> &client,
                Allocator *allocator) noexcept
            {

                if (client == nullptr)
                {
                    AWS_LOGF_ERROR(AWS_LS_MQTT5_GENERAL, "Failed to create Mqtt5ListenerCore. Invalid Client.");
                    aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
                    return nullptr;
                }

                /* Copied from MqttClient.cpp:ln754 (MqttClient::NewConnection) */
                // As the constructor is private, make share would not work here. We do make_share manually.
                Mqtt5ListenerCore *toSeat =
                    reinterpret_cast<Mqtt5ListenerCore *>(aws_mem_acquire(allocator, sizeof(Mqtt5ListenerCore)));
                if (!toSeat)
                {
                    return nullptr;
                }

                toSeat = new (toSeat) Mqtt5ListenerCore(options, client, allocator);
                // Creation failed, make sure we release the allocated memory
                if (!*toSeat)
                {
                    Crt::Delete(toSeat, allocator);
                    return nullptr;
                }

                std::shared_ptr<Mqtt5ListenerCore> shared_listener = std::shared_ptr<Mqtt5ListenerCore>(
                    toSeat, [allocator](Mqtt5ListenerCore *listener) { Crt::Delete(listener, allocator); });
                shared_listener->m_selfReference = shared_listener;
                return shared_listener;
            }

            Mqtt5ListenerCore::operator bool() const noexcept { return m_listener != nullptr; }

            int Mqtt5ListenerCore::LastError() const noexcept { return aws_last_error(); }

            void Mqtt5ListenerCore::Close() noexcept
            {
                std::lock_guard<std::recursive_mutex> lk(m_callback_lock);
                m_callbackFlag = CallbackFlag::IGNORE;
                if (m_listener != nullptr)
                {
                    aws_mqtt5_listener_release(m_listener);
                    m_listener = nullptr;
                }
            }

            Mqtt5ListenerCore::~Mqtt5ListenerCore() {}

            Mqtt5ListenerCore::Mqtt5ListenerCore(
                const Mqtt5ListenerOptions &options,
                const std::shared_ptr<Mqtt5Client> &client,
                Allocator *allocator) noexcept
                : m_callbackFlag(Mqtt5ListenerCore::CallbackFlag::INVOKE), m_listener(nullptr), m_allocator(allocator)
            {

                if (client == nullptr)
                {
                    AWS_LOGF_ERROR(AWS_LS_MQTT5_GENERAL, "Failed to create Mqtt5ListenerCore. Invalid Client.");
                    aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
                    return;
                }

                m_mqtt5Client = client->shared_from_this();
                this->onConnectionFailure = options.onConnectionFailure;
                this->onConnectionSuccess = options.onConnectionSuccess;
                this->onDisconnection = options.onDisconnection;
                this->onListenerPublishReceived = options.onListenerPublishReceived;
                this->onStopped = options.onStopped;
                this->onAttemptingConnect = options.onAttemptingConnect;
                this->onListenerTermination = options.onListenerTermination;
                this->termination_userdata = options.termination_userdata;

                aws_mqtt5_listener_config listener_config;
                listener_config.listener_callbacks.lifecycle_event_handler =
                    &Mqtt5ListenerCore::s_lifeCycleEventCallback;
                listener_config.listener_callbacks.lifecycle_event_handler_user_data = this;

                listener_config.listener_callbacks.listener_publish_received_handler =
                    &Mqtt5ListenerCore::s_publishReceivedCallback;
                listener_config.listener_callbacks.listener_publish_received_handler_user_data = this;

                listener_config.termination_callback = &Mqtt5ListenerCore::s_listenerTerminationCompletion;
                listener_config.termination_callback_user_data = this;

                listener_config.client = m_mqtt5Client->m_client_core->m_client;

                m_listener = aws_mqtt5_listener_new(allocator, &listener_config);
            }

            void Mqtt5ListenerCore::s_lifeCycleEventCallback(const aws_mqtt5_client_lifecycle_event *event)
            {
                Mqtt5ListenerCore *listener = reinterpret_cast<Mqtt5ListenerCore *>(event->user_data);
                if (listener == nullptr)
                {
                    AWS_LOGF_ERROR(
                        AWS_LS_MQTT5_GENERAL, "Listener Lifecycle Event: error retrieving callback userdata. ");
                    return;
                }

                std::lock_guard<std::recursive_mutex> lock(listener->m_callback_lock);
                if (listener->m_callbackFlag != Mqtt5ListenerCore::CallbackFlag::INVOKE)
                {
                    AWS_LOGF_DEBUG(
                        AWS_LS_MQTT5_GENERAL,
                        "Listener Lifecycle Event: mqtt5 listener is not valid, revoke the callbacks.");
                    return;
                }

                switch (event->event_type)
                {
                    case AWS_MQTT5_CLET_STOPPED:
                        AWS_LOGF_DEBUG(AWS_LS_MQTT5_GENERAL, "Listener Lifecycle Event: listener Stopped!");
                        if (listener->onStopped != nullptr)
                        {
                            OnStoppedEventData eventData;
                            listener->onStopped(eventData);
                        }
                        break;

                    case AWS_MQTT5_CLET_ATTEMPTING_CONNECT:
                        AWS_LOGF_DEBUG(AWS_LS_MQTT5_GENERAL, "Listener Lifecycle Event: Attempting Connect!");
                        if (listener->onAttemptingConnect != nullptr)
                        {
                            OnAttemptingConnectEventData eventData;
                            listener->onAttemptingConnect(eventData);
                        }
                        break;

                    case AWS_MQTT5_CLET_CONNECTION_FAILURE:
                        AWS_LOGF_DEBUG(
                            AWS_LS_MQTT5_GENERAL,
                            "Listener Lifecycle Event: Connection Failure with error code: %d(%s)!",
                            event->error_code,
                            aws_error_debug_str(event->error_code));
                        if (listener->onConnectionFailure != nullptr)
                        {
                            OnConnectionFailureEventData eventData;
                            eventData.errorCode = event->error_code;
                            std::shared_ptr<ConnAckPacket> packet = nullptr;
                            if (event->connack_data != NULL)
                            {
                                packet = Aws::Crt::MakeShared<ConnAckPacket>(
                                    listener->m_allocator, *event->connack_data, listener->m_allocator);
                                eventData.connAckPacket = packet;
                            }
                            listener->onConnectionFailure(eventData);
                        }
                        break;

                    case AWS_MQTT5_CLET_CONNECTION_SUCCESS:
                        AWS_LOGF_DEBUG(AWS_LS_MQTT5_GENERAL, "Listener Lifecycle Event: Connection Success!");
                        if (listener->onConnectionSuccess != nullptr)
                        {
                            OnConnectionSuccessEventData eventData;

                            std::shared_ptr<ConnAckPacket> packet = nullptr;
                            if (event->connack_data != NULL)
                            {
                                packet = Aws::Crt::MakeShared<ConnAckPacket>(ApiAllocator(), *event->connack_data);
                            }

                            std::shared_ptr<NegotiatedSettings> neg_settings = nullptr;
                            if (event->settings != NULL)
                            {
                                neg_settings =
                                    Aws::Crt::MakeShared<NegotiatedSettings>(ApiAllocator(), *event->settings);
                            }

                            eventData.connAckPacket = packet;
                            eventData.negotiatedSettings = neg_settings;
                            listener->onConnectionSuccess(eventData);
                        }
                        break;

                    case AWS_MQTT5_CLET_DISCONNECTION:
                        AWS_LOGF_DEBUG(
                            AWS_LS_MQTT5_GENERAL,
                            "Listener Lifecycle Event: Disconnect with error code: %d(%s)",
                            event->error_code,
                            aws_error_debug_str(event->error_code));
                        if (listener->onDisconnection != nullptr)
                        {
                            OnDisconnectionEventData eventData;
                            std::shared_ptr<DisconnectPacket> disconnection = nullptr;
                            if (event->disconnect_data != nullptr)
                            {
                                disconnection = Aws::Crt::MakeShared<DisconnectPacket>(
                                    listener->m_allocator, *event->disconnect_data, listener->m_allocator);
                            }
                            eventData.errorCode = event->error_code;
                            eventData.disconnectPacket = disconnection;
                            listener->onDisconnection(eventData);
                        }
                        break;
                }
            }

            bool Mqtt5ListenerCore::s_publishReceivedCallback(
                const aws_mqtt5_packet_publish_view *publish,
                void *user_data)
            {
                AWS_LOGF_DEBUG(AWS_LS_MQTT5_GENERAL, "on listener publish received callback");
                Mqtt5ListenerCore *listener = reinterpret_cast<Mqtt5ListenerCore *>(user_data);
                if (listener == nullptr)
                {
                    AWS_LOGF_ERROR(
                        AWS_LS_MQTT5_GENERAL, "Listener Publish Received Event: Error retrieving callback userdata. ");
                    return false;
                }

                if (listener->onListenerPublishReceived == nullptr)
                {
                    AWS_LOGF_DEBUG(
                        AWS_LS_MQTT5_GENERAL,
                        "Listener Publish Received Event: The publish received callback is not set.");
                    return false;
                }

                std::lock_guard<std::recursive_mutex> lock(listener->m_callback_lock);
                if (listener->m_callbackFlag != Mqtt5ListenerCore::CallbackFlag::INVOKE)
                {
                    AWS_LOGF_DEBUG(
                        AWS_LS_MQTT5_GENERAL,
                        "Listener Publish Received Event: Mqtt5 client is not valid, revoke the callbacks.");
                    return false;
                }

                PublishReceivedEventData eventData;
                if (publish != nullptr)
                {
                    std::shared_ptr<PublishPacket> packet =
                        std::make_shared<PublishPacket>(*publish, listener->m_allocator);
                    eventData.publishPacket = packet;
                }
                else
                {
                    AWS_LOGF_ERROR(
                        AWS_LS_MQTT5_GENERAL, "Listener Publish Received Event: Failed to access Publish packet view.");
                    return false;
                }

                return listener->onListenerPublishReceived(eventData);
            }

            void Mqtt5ListenerCore::s_listenerTerminationCompletion(void *complete_ctx)
            {
                Mqtt5ListenerCore *listener = reinterpret_cast<Mqtt5ListenerCore *>(complete_ctx);
                if (listener->onListenerTermination)
                {
                    listener->onListenerTermination(listener->termination_userdata);
                }
                /* release mqtt5Client */
                listener->m_mqtt5Client.reset();
                /* release Mqtt5ListenerCore reference */
                listener->m_selfReference.reset();
            }

            ScopedResource<Mqtt5Listener> Mqtt5Listener::NewMqtt5Listener(
                const Mqtt5ListenerOptions &options,
                const std::shared_ptr<Mqtt5Client> &client,
                Allocator *allocator) noexcept
            {
                if (client == nullptr)
                {
                    AWS_LOGF_ERROR(AWS_LS_MQTT5_GENERAL, "Failed to create Mqtt5Listener. Invalid Client.");
                    aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
                    return nullptr;
                }

                /* Copied from MqttClient.cpp:ln754 (MqttClient::NewConnection) */
                // As the constructor is private, make share would not work here. We do make_share manually.
                Mqtt5Listener *toSeat =
                    reinterpret_cast<Mqtt5Listener *>(aws_mem_acquire(allocator, sizeof(Mqtt5Listener)));
                if (!toSeat)
                {
                    return nullptr;
                }

                toSeat = new (toSeat) Mqtt5Listener(options, client, allocator);
                // Creation failed, make sure we release the allocated memory
                if (!*toSeat)
                {
                    Crt::Delete(toSeat, allocator);
                    return nullptr;
                }
                // //Custom Resource deleter
                // struct Deleter {
                //     //Called by unique_ptr to destroy/free the Resource
                //     void operator()(Mqtt5Listener* listener) {
                //         Crt::Delete(listener, allocator);
                //     }
                // };

                ScopedResource<Mqtt5Listener> uniq_listener(
                    toSeat, [allocator](Mqtt5Listener *listener) { Crt::Delete(listener, allocator); });

                return uniq_listener;
            }

            Mqtt5Listener::Mqtt5Listener(
                const Mqtt5ListenerOptions &options,
                const std::shared_ptr<Mqtt5Client> &client,
                Allocator *allocator) noexcept
                : m_allocator(allocator)
            {
                m_listener_core = Mqtt5ListenerCore::NewMqtt5ListenerCore(options, client, m_allocator);
            }

            Mqtt5::Mqtt5Listener::~Mqtt5Listener()
            {
                if (m_listener_core != nullptr)
                {
                    m_listener_core->Close();
                }
            }

            Mqtt5ListenerOptions::Mqtt5ListenerOptions() noexcept
                : onAttemptingConnect(nullptr), onConnectionFailure(nullptr), onConnectionSuccess(nullptr),
                  onDisconnection(nullptr), onStopped(nullptr), onListenerPublishReceived(nullptr),
                  onListenerTermination(nullptr), termination_userdata(nullptr)
            {
            }

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::WithListenerConnectionSuccessCallback(
                OnConnectionSuccessHandler callback) noexcept
            {
                onConnectionSuccess = std::move(callback);
                return *this;
            }

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::WithListenerConnectionFailureCallback(
                OnConnectionFailureHandler callback) noexcept
            {
                onConnectionFailure = std::move(callback);
                return *this;
            }

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::WithListenerDisconnectionCallback(
                OnDisconnectionHandler callback) noexcept
            {
                onDisconnection = std::move(callback);
                return *this;
            }

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::WithListenerStoppedCallback(OnStoppedHandler callback) noexcept
            {
                onStopped = std::move(callback);
                return *this;
            }

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::WithListenerAttemptingConnectCallback(
                OnAttemptingConnectHandler callback) noexcept
            {
                onAttemptingConnect = std::move(callback);
                return *this;
            }

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::WithListenerPublishReceivedCallback(
                OnListenerPublishReceivedHandler callback) noexcept
            {
                onListenerPublishReceived = std::move(callback);
                return *this;
            }

            Mqtt5ListenerOptions::~Mqtt5ListenerOptions() {}

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::WithListenerTerminationCallback(
                OnListenerTerminationHandler callback,
                void *user_data) noexcept
            {
                onListenerTermination = callback;
                termination_userdata = user_data;
                return *this;
            }
        } // namespace Mqtt5

    } // namespace Crt
} // namespace Aws
