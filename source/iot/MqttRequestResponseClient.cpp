/**
* Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
* SPDX-License-Identifier: Apache-2.0.
*/

#include <aws/iot/MqttRequestResponseClient.h>

#include <aws/crt/mqtt/Mqtt5Client.h>
#include <aws/crt/mqtt/MqttConnection.h>

#include <aws/common/ref_count.h>
#include <aws/common/rw_lock.h>

namespace Aws
{
namespace Iot
{
namespace RequestResponse
{

    class AWS_CRT_CPP_API StreamingOperationImpl {
      public:
        StreamingOperationImpl(StreamingOperationOptions &&options, struct aws_event_loop *protocolLoop);
        virtual ~StreamingOperationImpl();

        void seatStream(struct aws_mqtt_rr_client_operation *stream);

        void close();

        static void onIncomingPublish(StreamingOperationImpl *impl);
        static void onSubscriptionStatusEvent(StreamingOperationImpl *impl);

      private:

        Aws::Crt::Allocator *m_allocator;

        StreamingOperationOptions m_Config;

        struct aws_mqtt_rr_client_operation *m_stream;

        struct aws_event_loop *m_protocolLoop;

        struct aws_rw_lock m_lock;

        bool m_closed;
    };

    StreamingOperationImpl::StreamingOperationImpl(StreamingOperationOptions &&options, struct aws_event_loop *protocolLoop) {

    }

    StreamingOperationImpl::~StreamingOperationImpl() {

    }

    void StreamingOperationImpl::seatStream(struct aws_mqtt_rr_client_operation *stream) {
        m_stream = stream;
    }

    void StreamingOperationImpl::close() {
        aws_mqtt_rr_client_operation_release(m_stream);
    }

    void StreamingOperationImpl::onIncomingPublish(StreamingOperationImpl *impl) {

    }

    void StreamingOperationImpl::onSubscriptionStatusEvent(StreamingOperationImpl *impl) {

    }

    //////////////////////////////////////////////////////////

    class StreamingOperation : public IStreamingOperation {
      public:

        StreamingOperation(StreamingOperationOptions &&options, struct aws_event_loop *protocolLoop);
        virtual ~StreamingOperation();

      private:

        std::shared_ptr<StreamingOperationImpl> m_impl;
    };


    StreamingOperation::StreamingOperation(StreamingOperationOptions &&options, struct aws_event_loop *protocolLoop) {

    }

    StreamingOperation::~StreamingOperation() {
        m_impl->close();
    }

    //////////////////////////////////////////////////////////

    struct IncompleteRequest
    {
        struct aws_allocator *m_allocator;

        UnmodeledResultHandler m_handler;
    };

    static void s_completeRequestWithError(struct IncompleteRequest *incompleteRequest, int errorCode) {
        UnmodeledResult result(errorCode);
        incompleteRequest->m_handler(std::move(result));
    }

    static void s_completeRequestWithSuccess(struct IncompleteRequest *incompleteRequest, const struct aws_byte_cursor *response_topic,
                                             const struct aws_byte_cursor *payload) {
        Response response;
        response.topic = *response_topic;
        response.payload = *payload;

        UnmodeledResult result(response);
        incompleteRequest->m_handler(std::move(result));
    }

    static void s_onRequestComplete(const struct aws_byte_cursor *response_topic,
                                    const struct aws_byte_cursor *payload,
                                    int error_code,
                                    void *user_data) {
        struct IncompleteRequest *incompleteRequest = static_cast<struct IncompleteRequest *>(user_data);

        if (error_code != AWS_ERROR_SUCCESS) {
            s_completeRequestWithError(incompleteRequest, error_code);
        } else {
            s_completeRequestWithSuccess(incompleteRequest, response_topic, payload);
        }

        Aws::Crt::Delete(incompleteRequest, incompleteRequest->m_allocator);
    }

    class AWS_CRT_CPP_API MqttRequestResponseClientImpl
    {
      public:
        MqttRequestResponseClientImpl(Aws::Crt::Allocator *allocator) noexcept;
        ~MqttRequestResponseClientImpl();

        void seatClient(struct aws_mqtt_request_response_client *client);

        void close() noexcept;

        int submitRequest(
            const aws_mqtt_request_operation_options &requestOptions,
            UnmodeledResultHandler &&resultHandler) noexcept;

        std::shared_ptr<IStreamingOperation> createStream(StreamingOperationOptions &&options);

        Aws::Crt::Allocator *getAllocator() const { return m_allocator; }

      private:

        Aws::Crt::Allocator *m_allocator;

        struct aws_mqtt_request_response_client *m_client;
    };

    MqttRequestResponseClientImpl::MqttRequestResponseClientImpl(Aws::Crt::Allocator *allocator
        ) noexcept :
        m_allocator(allocator),
        m_client(nullptr)
    {
    }

    MqttRequestResponseClientImpl::~MqttRequestResponseClientImpl() {
        AWS_FATAL_ASSERT(m_client == nullptr);
    }

    void MqttRequestResponseClientImpl::seatClient(struct aws_mqtt_request_response_client *client) {
        m_client = client;
    }

    void MqttRequestResponseClientImpl::close() noexcept {
        aws_mqtt_request_response_client_release(m_client);
        m_client = nullptr;
    }

    int MqttRequestResponseClientImpl::submitRequest(const aws_mqtt_request_operation_options &requestOptions, UnmodeledResultHandler &&resultHandler) noexcept
    {
        IncompleteRequest *incompleteRequest = Aws::Crt::New<IncompleteRequest>(m_allocator);
        incompleteRequest->m_allocator = m_allocator;
        incompleteRequest->m_handler = std::move(resultHandler);

        struct aws_mqtt_request_operation_options rawOptions = requestOptions;
        rawOptions.completion_callback = s_onRequestComplete;
        rawOptions.user_data = incompleteRequest;

        int result = aws_mqtt_request_response_client_submit_request(m_client, &rawOptions);
        if (result) {
            Aws::Crt::Delete(incompleteRequest, incompleteRequest->m_allocator);
        }

        return result;
    }

    std::shared_ptr<IStreamingOperation> MqttRequestResponseClientImpl::createStream(StreamingOperationOptions &&options) {
        return nullptr;
    }

    //////////////////////////////////////////////////////////

    static void s_onClientTermination(void *user_data) {
        auto *impl = static_cast<MqttRequestResponseClientImpl *>(user_data);

        Aws::Crt::Delete(impl, impl->getAllocator());
    }

    MqttRequestResponseClient *MqttRequestResponseClient::newFrom5(const Aws::Crt::Mqtt5::Mqtt5Client &protocolClient, RequestResponseClientOptions &&options, Aws::Crt::Allocator *allocator) {
        auto clientImpl = Aws::Crt::New<MqttRequestResponseClientImpl>(allocator, allocator);

        struct aws_mqtt_request_response_client_options rrClientOptions;
        AWS_ZERO_STRUCT(rrClientOptions);
        rrClientOptions.max_request_response_subscriptions = options.maxRequestResponseSubscriptions;
        rrClientOptions.max_streaming_subscriptions = options.maxStreamingSubscriptions;
        rrClientOptions.operation_timeout_seconds = options.operationTimeoutInSeconds;
        rrClientOptions.terminated_callback = s_onClientTermination;
        rrClientOptions.user_data = clientImpl;

        struct aws_mqtt_request_response_client *rrClient = aws_mqtt_request_response_client_new_from_mqtt5_client(allocator, protocolClient.GetUnderlyingHandle(), &rrClientOptions);
        clientImpl->seatClient(rrClient);

        // Can't use Aws::Crt::New because constructor is private and I don't want to change that
        MqttRequestResponseClient *client = reinterpret_cast<MqttRequestResponseClient *>(aws_mem_acquire(allocator, sizeof(MqttRequestResponseClient)));

        return new (client) MqttRequestResponseClient(allocator, clientImpl);
    }


    MqttRequestResponseClient *MqttRequestResponseClient::newFrom311(const Aws::Crt::Mqtt::MqttConnection &protocolClient, RequestResponseClientOptions &&options, Aws::Crt::Allocator *allocator) {
        auto clientImpl = Aws::Crt::New<MqttRequestResponseClientImpl>(allocator, allocator);

        struct aws_mqtt_request_response_client_options rrClientOptions;
        AWS_ZERO_STRUCT(rrClientOptions);
        rrClientOptions.max_request_response_subscriptions = options.maxRequestResponseSubscriptions;
        rrClientOptions.max_streaming_subscriptions = options.maxStreamingSubscriptions;
        rrClientOptions.operation_timeout_seconds = options.operationTimeoutInSeconds;
        rrClientOptions.terminated_callback = s_onClientTermination;
        rrClientOptions.user_data = clientImpl;

        struct aws_mqtt_request_response_client *rrClient = aws_mqtt_request_response_client_new_from_mqtt311_client(allocator, protocolClient.??(), &rrClientOptions);
        clientImpl->seatClient(rrClient);

        // Can't use Aws::Crt::New because constructor is private and I don't want to change that
        MqttRequestResponseClient *client = reinterpret_cast<MqttRequestResponseClient *>(aws_mem_acquire(allocator, sizeof(MqttRequestResponseClient)));

        return new (client) MqttRequestResponseClient(allocator, clientImpl);
    }

    int MqttRequestResponseClient::submitRequest(const aws_mqtt_request_operation_options &requestOptions, UnmodeledResultHandler &&resultHandler) {
        return m_impl->submitRequest(requestOptions, std::move(resultHandler));
    }

    std::shared_ptr<IStreamingOperation> MqttRequestResponseClient::createStream(StreamingOperationOptions &&options) {
        return m_impl->createStream(std::move(options));
    }

    MqttRequestResponseClient::MqttRequestResponseClient(Aws::Crt::Allocator *allocator, MqttRequestResponseClientImpl *impl) :
        m_allocator(allocator),
        m_impl(impl)
    {
    }

    MqttRequestResponseClient::~MqttRequestResponseClient() {
        m_impl->close();
    }
}
}
}

