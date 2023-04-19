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

            std::shared_ptr<Mqtt5ListenerCore> Aws::Crt::Mqtt5::Mqtt5ListenerCore::NewMqtt5ListenerCore(
                const Mqtt5ListenerOptions &options,
                const std::shared_ptr<Mqtt5Client> &client,
                Allocator *allocator) noexcept
            {

                if (client == nullptr)
                {
                    AWS_LOGF_DEBUG(AWS_LS_MQTT5_GENERAL, "Failed to create Mqtt5ListenerCore. Invalid Client.");
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
                shared_listener->m_selfReference = toSeat->shared_from_this();
                return shared_listener;
            }

            Mqtt5ListenerCore::operator bool() const noexcept { return m_listener != nullptr; }

            int Mqtt5ListenerCore::LastError() const noexcept { return aws_last_error(); }

            void Mqtt5ListenerCore::Subscribe(Crt::String topic, Crt::Mqtt5::OnPublishReceivedHandler callback) noexcept
            {
                m_subscriptionMap[topic] = callback;
            }

            void Mqtt5ListenerCore::Unsubscribe(Crt::String topic) noexcept
            {
                if (m_subscriptionMap.find(topic) != m_subscriptionMap.end())
                {
                    m_subscriptionMap.erase(topic);
                }
            }

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
                    AWS_LOGF_DEBUG(AWS_LS_MQTT5_GENERAL, "Failed to create Mqtt5ListenerCore. Invalid Client.");
                    aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
                    return;
                }

                m_mqtt5Client = client->shared_from_this();
                if (options.onConnectionFailure)
                {
                    this->onConnectionFailure = options.onConnectionFailure;
                }

                if (options.onConnectionSuccess)
                {
                    this->onConnectionSuccess = options.onConnectionSuccess;
                }

                if (options.onDisconnection)
                {
                    this->onDisconnection = options.onDisconnection;
                }

                if (options.onListenerPublishReceived)
                {
                    this->onListenerPublishReceived = options.onListenerPublishReceived;
                }

                if (options.onStopped)
                {
                    this->onStopped = options.onStopped;
                }

                if (options.onAttemptingConnect)
                {
                    this->onAttemptingConnect = options.onAttemptingConnect;
                }

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
                    AWS_LOGF_INFO(AWS_LS_MQTT5_GENERAL, "Lifecycle event: error retrieving callback userdata. ");
                    return;
                }

                std::lock_guard<std::recursive_mutex> lock(listener->m_callback_lock);
                if (listener->m_callbackFlag != Mqtt5ListenerCore::CallbackFlag::INVOKE)
                {
                    AWS_LOGF_INFO(
                        AWS_LS_MQTT5_GENERAL, "Lifecycle event: mqtt5 listener is not valid, revoke the callbacks.");
                    return;
                }

                switch (event->event_type)
                {
                    case AWS_MQTT5_CLET_STOPPED:
                        AWS_LOGF_INFO(AWS_LS_MQTT5_GENERAL, "Lifecycle event: listener Stopped!");
                        if (listener->onStopped != nullptr)
                        {
                            OnStoppedEventData eventData;
                            listener->onStopped(eventData);
                        }
                        break;

                    case AWS_MQTT5_CLET_ATTEMPTING_CONNECT:
                        AWS_LOGF_INFO(AWS_LS_MQTT5_GENERAL, "Lifecycle event: Attempting Connect!");
                        if (listener->onAttemptingConnect != nullptr)
                        {
                            OnAttemptingConnectEventData eventData;
                            listener->onAttemptingConnect(eventData);
                        }
                        break;

                    case AWS_MQTT5_CLET_CONNECTION_FAILURE:
                        AWS_LOGF_INFO(AWS_LS_MQTT5_GENERAL, "Lifecycle event: Connection Failure!");
                        AWS_LOGF_INFO(
                            AWS_LS_MQTT5_GENERAL,
                            "  Error Code: %d(%s)",
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
                        AWS_LOGF_INFO(AWS_LS_MQTT5_GENERAL, "Lifecycle event: Connection Success!");
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
                        AWS_LOGF_INFO(
                            AWS_LS_MQTT5_GENERAL,
                            "  Error Code: %d(%s)",
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
                AWS_LOGF_INFO(AWS_LS_MQTT5_GENERAL, "on listener publish received callback");
                Mqtt5ListenerCore *listener = reinterpret_cast<Mqtt5ListenerCore *>(user_data);
                if (listener == nullptr)
                {
                    AWS_LOGF_INFO(AWS_LS_MQTT5_GENERAL, "Lifecycle event: error retrieving callback userdata. ");
                    return false;
                }

                std::lock_guard<std::recursive_mutex> lock(listener->m_callback_lock);
                if (listener->m_callbackFlag != Mqtt5ListenerCore::CallbackFlag::INVOKE)
                {
                    AWS_LOGF_INFO(
                        AWS_LS_MQTT5_GENERAL,
                        "Publish Received Event: mqtt5 client is not valid, revoke the callbacks.");
                    return false;
                }

                PublishReceivedEventData eventData;
                if (publish != nullptr)
                {
                    std::shared_ptr<PublishPacket> packet =
                        std::make_shared<PublishPacket>(*publish, listener->m_allocator);
                    PublishReceivedEventData eventData;
                    eventData.publishPacket = packet;
                }
                else
                {
                    AWS_LOGF_ERROR(AWS_LS_MQTT5_GENERAL, "Failed to access Publish packet view.");
                    return false;
                }

                /* If the listener publish received is overwritten, then call the overwritten function. */
                if (listener->onListenerPublishReceived != nullptr)
                {
                    return listener->onListenerPublishReceived(eventData);
                }
                else // Default mechanism. Look up the topic map and invoke callbacks
                {
                    Crt::String topic = eventData.publishPacket->getTopic();
                    if (listener->m_subscriptionMap.find(topic) != listener->m_subscriptionMap.end())
                    {
                        listener->m_subscriptionMap[topic](eventData);
                        return true;
                    }
                }

                return false;
            }

            void Mqtt5ListenerCore::s_listenerTerminationCompletion(void *complete_ctx)
            {
                Mqtt5ListenerCore *listener = reinterpret_cast<Mqtt5ListenerCore *>(complete_ctx);
                /* release mqtt5Client */
                listener->m_mqtt5Client.reset();
                /* release Mqtt5ListenerCore reference */
                listener->m_selfReference.reset();
            }

            std::shared_ptr<Mqtt5Listener> Mqtt5Listener::NewMqtt5Listener(
                const Mqtt5ListenerOptions &options,
                const std::shared_ptr<Mqtt5Client> &client,
                Allocator *allocator) noexcept
            {
                if (client == nullptr)
                {
                    AWS_LOGF_DEBUG(AWS_LS_MQTT5_GENERAL, "Failed to create Mqtt5Listener. Invalid Client.");
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

                std::shared_ptr<Mqtt5Listener> shared_listener = std::shared_ptr<Mqtt5Listener>(
                    toSeat, [allocator](Mqtt5Listener *listener) { Crt::Delete(listener, allocator); });
                return shared_listener;
            }

            Mqtt5Listener::Mqtt5Listener(
                const Mqtt5ListenerOptions &options,
                const std::shared_ptr<Mqtt5Client> &client,
                Allocator *allocator) noexcept
                : m_allocator(allocator)
            {
                m_listener_core = Mqtt5ListenerCore::NewMqtt5ListenerCore(options, client, m_allocator);
            }

            void Mqtt5Listener::Subscribe(Crt::String topic, Crt::Mqtt5::OnPublishReceivedHandler callback) noexcept {
                if(m_listener_core!=nullptr)
                {
                    m_listener_core->Subscribe(topic, callback);
                }
            }
            void Mqtt5Listener::Unsubscribe(Crt::String topic) noexcept {
                if(m_listener_core != nullptr)
                {
                    m_listener_core->Unsubscribe(topic);
                }
            }
            Mqtt5::Mqtt5Listener::~Mqtt5Listener()
            {
                if (m_listener_core != nullptr)
                {
                    m_listener_core->Close();
                }
            }

            Mqtt5ListenerOptions::Mqtt5ListenerOptions() noexcept
            {
                onAttemptingConnect = NULL;
                onConnectionFailure = NULL;
                onConnectionSuccess = NULL;
                onDisconnection = NULL;
                onStopped = NULL;
                onListenerPublishReceived = NULL;
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

        } // namespace Mqtt5

    } // namespace Crt
} // namespace Aws
