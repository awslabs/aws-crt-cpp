#include "Mqtt5Listener.h"
#include <aws/crt/mqtt/Mqtt5Listener.h>

namespace Aws
{
    namespace Crt
    {
        namespace Mqtt5
        {
            std::shared_ptr<Mqtt5Listener> Aws::Crt::Mqtt5::Mqtt5Listener::NewMqtt5Listener(
                const Mqtt5ListenerOptions &options,
                const std::shared_ptr<Mqtt5Client> client,
                Allocator *allocator) noexcept
            {
                return std::shared_ptr<Mqtt5Listener>();
            }

            Mqtt5Listener::operator bool() const noexcept
            {
                return m_listener != nullptr;
            }

            int Mqtt5Listener::LastError() const noexcept
            {
                return aws_last_error();
            }

            Mqtt5Listener::~Mqtt5Listener() {}

            Mqtt5Listener::Mqtt5Listener(
                const Mqtt5ListenerOptions &options,
                const std::shared_ptr<Mqtt5Client> client,
                Allocator *allocator = ApiAllocator()) noexcept
            {
            }

            void Mqtt5Listener::s_lifeCycleEventCallback(const aws_mqtt5_client_lifecycle_event *event) {}

            bool Mqtt5Listener::s_publishReceivedCallback(const aws_mqtt5_packet_publish_view *publish, void *user_data)
            {
                return false;
            }

            void Mqtt5Listener::s_clientTerminationCompletion(void *complete_ctx) {}

            Mqtt5ListenerOptions::Mqtt5ListenerOptions(Crt::Allocator *allocator) noexcept {}

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::withClientConnectionSuccessCallback(
                OnConnectionSuccessHandler callback) noexcept
            {
                // TODO: insert return statement here
            }

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::withClientConnectionFailureCallback(
                OnConnectionFailureHandler callback) noexcept
            {
                // TODO: insert return statement here
            }

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::withClientDisconnectionCallback(
                OnDisconnectionHandler callback) noexcept
            {
                // TODO: insert return statement here
            }

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::withClientStoppedCallback(OnStoppedHandler callback) noexcept
            {
                // TODO: insert return statement here
            }

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::withClientAttemptingConnectCallback(
                OnAttemptingConnectHandler callback) noexcept
            {
                // TODO: insert return statement here
            }

            Mqtt5ListenerOptions &Mqtt5ListenerOptions::withListenerPublishReceivedCallback(
                OnListenerPublishReceivedHandler callback) noexcept
            {
                // TODO: insert return statement here
            }

            bool Mqtt5ListenerOptions::initializeRawOptions(aws_mqtt5_client_options &raw_options) const noexcept
            {
                return false;
            }

            Mqtt5ListenerOptions::~Mqtt5ListenerOptions() {}

        } // namespace Mqtt5
    }     // namespace Crt
} // namespace Aws