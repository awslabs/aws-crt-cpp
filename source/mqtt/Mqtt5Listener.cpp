#include <aws/crt/mqtt/Mqtt5Listener.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/crt/mqtt/Mqtt5Types.h>

#include <aws/mqtt/v5/mqtt5_listener.h>

namespace Aws
{
    namespace Crt
    {
        namespace Mqtt5
        {
            class ScopedWriteLock;

            static bool ProcessNativeClientAndCreateMqtt5Listener(
                aws_mqtt5_client *native_client,
                void *listener_options,
                void *out_result)
            {
                AWS_FATAL_ASSERT(!native_client);
                AWS_FATAL_ASSERT(!listener_options);
                aws_mqtt5_listener_config *config = static_cast<aws_mqtt5_listener_config *>(listener_options);
                config->client = native_client;

                out_result = aws_mqtt5_listener_new(g_allocator, config);
                return true;
            }

            std::shared_ptr<Mqtt5Listener> Aws::Crt::Mqtt5::Mqtt5Listener::NewMqtt5Listener(
                const Mqtt5ListenerOptions &options,
                const std::shared_ptr<Mqtt5Client> client,
                Allocator *allocator) noexcept
            {
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
                shared_listener->m_selfReference = shared_listener;
                return shared_listener;
            }

            Mqtt5Listener::operator bool() const noexcept
            {
                return m_listener != nullptr;
            }

            int Mqtt5Listener::LastError() const noexcept
            {
                return aws_last_error();
            }

            void Mqtt5Listener::Close() noexcept
            {
                ScopedWriteLock lock(&m_listener_lock);
                if (m_listener != nullptr)
                {
                    aws_mqtt5_listener_release(m_listener);
                    m_listener = nullptr;
                }
            }

            Mqtt5Listener::~Mqtt5Listener() {}

            Mqtt5Listener::Mqtt5Listener(
                const Mqtt5ListenerOptions &options,
                const std::shared_ptr<Mqtt5Client> client,
                Allocator *allocator) noexcept
            {
                m_mqtt5Client = client;
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
                listener_config.listener_callbacks.lifecycle_event_handler = &Mqtt5Listener::s_lifeCycleEventCallback;
                listener_config.listener_callbacks.lifecycle_event_handler_user_data = this;

                listener_config.listener_callbacks.listener_publish_received_handler =
                    &Mqtt5Listener::s_publishReceivedCallback;
                listener_config.listener_callbacks.listener_publish_received_handler_user_data = this;

                listener_config.termination_callback = &Mqtt5Listener::s_listenerTerminationCompletion;
                listener_config.termination_callback_user_data = this;

                AWS_FATAL_ASSERT(aws_rw_lock_init(&m_listener_lock) == AWS_OP_SUCCESS);

                m_mqtt5Client->ProceedOnNativeClient(
                    &ProcessNativeClientAndCreateMqtt5Listener, &listener_config, m_listener);
            }

            void Mqtt5Listener::s_lifeCycleEventCallback(const aws_mqtt5_client_lifecycle_event *event)
            {
                Mqtt5Listener *listener = reinterpret_cast<Mqtt5Listener *>(event->user_data);
                std::shared_ptr<Mqtt5Client> client = listener->m_mqtt5Client;
                AWS_ASSERT(listener != nullptr);
                AWS_ASSERT(client != nullptr);

                switch (event->event_type)
                {
                    case AWS_MQTT5_CLET_STOPPED:
                        AWS_LOGF_INFO(AWS_LS_MQTT5_GENERAL, "Lifecycle event: listener Stopped!");
                        if (listener->onStopped)
                        {
                            OnStoppedEventData eventData;
                            listener->onStopped(*client, eventData);
                        }
                        break;

                    case AWS_MQTT5_CLET_ATTEMPTING_CONNECT:
                        AWS_LOGF_INFO(AWS_LS_MQTT5_GENERAL, "Lifecycle event: Attempting Connect!");
                        if (listener->onAttemptingConnect)
                        {
                            OnAttemptingConnectEventData eventData;
                            listener->onAttemptingConnect(*client, eventData);
                        }
                        break;

                    case AWS_MQTT5_CLET_CONNECTION_FAILURE:
                        AWS_LOGF_INFO(AWS_LS_MQTT5_GENERAL, "Lifecycle event: Connection Failure!");
                        AWS_LOGF_INFO(
                            AWS_LS_MQTT5_GENERAL,
                            "  Error Code: %d(%s)",
                            event->error_code,
                            aws_error_debug_str(event->error_code));
                        if (listener->onConnectionFailure)
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
                            listener->onConnectionFailure(*client, eventData);
                        }
                        break;

                    case AWS_MQTT5_CLET_CONNECTION_SUCCESS:
                        AWS_LOGF_INFO(AWS_LS_MQTT5_GENERAL, "Lifecycle event: Connection Success!");
                        if (listener->onConnectionSuccess)
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
                            listener->onConnectionSuccess(*client, eventData);
                        }
                        break;

                    case AWS_MQTT5_CLET_DISCONNECTION:
                        AWS_LOGF_INFO(
                            AWS_LS_MQTT5_GENERAL,
                            "  Error Code: %d(%s)",
                            event->error_code,
                            aws_error_debug_str(event->error_code));
                        if (listener->onDisconnection)
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
                            listener->onDisconnection(*client, eventData);
                        }
                        break;
                }
            }

            bool Mqtt5Listener::s_publishReceivedCallback(const aws_mqtt5_packet_publish_view *publish, void *user_data)
            {
                AWS_LOGF_INFO(AWS_LS_MQTT5_GENERAL, "on publish recieved callback");
                Mqtt5Listener *listener = reinterpret_cast<Mqtt5Listener *>(user_data);
                std::shared_ptr<Mqtt5Client> client = listener->m_mqtt5Client;
                if (listener != nullptr && client != nullptr && listener->onListenerPublishReceived != nullptr)
                {
                    if (publish != NULL)
                    {
                        std::shared_ptr<PublishPacket> packet =
                            std::make_shared<PublishPacket>(*publish, listener->m_allocator);
                        PublishReceivedEventData eventData;
                        eventData.publishPacket = packet;
                        return listener->onListenerPublishReceived(*(listener->m_mqtt5Client), eventData);
                    }
                    else
                    {
                        AWS_LOGF_ERROR(AWS_LS_MQTT5_GENERAL, "Failed to access Publish packet view.");
                    }
                }
                return false;
            }

            void Mqtt5Listener::s_listenerTerminationCompletion(void *complete_ctx)
            {
                Mqtt5Listener *listener = reinterpret_cast<Mqtt5Listener *>(complete_ctx);
                /* release mqtt5Listener reference */
                listener->m_selfReference = nullptr;
                /* release mqtt5Client */
                listener->m_mqtt5Client = nullptr;
            }

            Mqtt5ListenerOptions::Mqtt5ListenerOptions(Crt::Allocator *allocator) noexcept : m_allocator(allocator) {}

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::withListenerConnectionSuccessCallback(
                OnConnectionSuccessHandler callback) noexcept
            {
                onConnectionSuccess = std::move(callback);
                return *this;
            }

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::withListenerConnectionFailureCallback(
                OnConnectionFailureHandler callback) noexcept
            {
                onConnectionFailure = std::move(callback);
                return *this;
            }

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::withListenerDisconnectionCallback(
                OnDisconnectionHandler callback) noexcept
            {
                onDisconnection = std::move(callback);
                return *this;
            }

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::withListenerStoppedCallback(OnStoppedHandler callback) noexcept
            {
                onStopped = std::move(callback);
                return *this;
            }

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::withListenerAttemptingConnectCallback(
                OnAttemptingConnectHandler callback) noexcept
            {
                onAttemptingConnect = std::move(callback);
                return *this;
            }

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::withListenerPublishReceivedCallback(
                OnListenerPublishReceivedHandler callback) noexcept
            {
                onListenerPublishReceived = std::move(callback);
                return *this;
            }

            Mqtt5ListenerOptions::~Mqtt5ListenerOptions() {}

        } // namespace Mqtt5
    }     // namespace Crt
} // namespace Aws