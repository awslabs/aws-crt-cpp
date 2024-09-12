/**
* Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
* SPDX-License-Identifier: Apache-2.0.
*/

#include <aws/iot/MqttRequestResponseClient.h>
#include <aws/iot/private/MqttRequestResponseClientImpl.h>

#include <aws/crt/mqtt/Mqtt5Client.h>
#include <aws/crt/mqtt/MqttConnection.h>

namespace Aws
{
namespace Iot
{
namespace RequestResponse
{

    struct IncompleteRequest
    {
        struct aws_allocator *m_allocator;

        struct aws_linked_list_node m_node;

        struct aws_ref_count m_refCount;

        std::shared_ptr<MqttRequestResponseClientImpl> m_clientImpl;

        UnmodeledResultHandler m_handler;
    };

    static void s_destroyIncompleteRequest(void *context) {
        struct IncompleteRequest *incompleteRequest = static_cast<struct IncompleteRequest *>(context);

        Aws::Crt::Delete(incompleteRequest, incompleteRequest->m_allocator);
    }

    static void s_completeRequestWithError(struct IncompleteRequest *incompleteRequest, int errorCode) {
        if (aws_linked_list_node_is_in_list(&incompleteRequest->m_node)) {
            aws_linked_list_remove(&incompleteRequest->m_node);
        }

        UnmodeledResult result(errorCode);
        incompleteRequest->m_handler(std::move(result));

        aws_ref_count_release(&incompleteRequest->m_refCount);
    }

    static void s_completeRequestWithSuccess(struct IncompleteRequest *incompleteRequest, const struct aws_byte_cursor *response_topic,
                                             const struct aws_byte_cursor *payload) {
        if (aws_linked_list_node_is_in_list(&incompleteRequest->m_node)) {
            aws_linked_list_remove(&incompleteRequest->m_node);
        }

        Response response;
        response.topic = *response_topic;
        response.payload = *payload;

        UnmodeledResult result(response);
        incompleteRequest->m_handler(std::move(result));

        aws_ref_count_release(&incompleteRequest->m_refCount);
    }

    static void s_onRequestComplete(const struct aws_byte_cursor *response_topic,
                                    const struct aws_byte_cursor *payload,
                                    int error_code,
                                    void *user_data) {
        struct IncompleteRequest *incompleteRequest = static_cast<struct IncompleteRequest *>(user_data);

        incompleteRequest->m_clientImpl->onRequestCompletion(incompleteRequest, response_topic, payload, error_code);
    }

    MqttRequestResponseClientImpl::MqttRequestResponseClientImpl(Aws::Crt::Allocator *allocator
        ) noexcept :
        m_allocator(allocator),
        m_client(nullptr),
        m_closed(false)
    {
        aws_rw_lock_init(&this->m_lock);
        aws_linked_list_init(&this->m_incompleteRequests);
    }

    MqttRequestResponseClientImpl::~MqttRequestResponseClientImpl() noexcept {
        aws_rw_lock_clean_up(&this->m_lock);

        AWS_FATAL_ASSERT(m_closed);
        AWS_FATAL_ASSERT(m_client == nullptr);
        AWS_FATAL_ASSERT(aws_linked_list_empty(&m_incompleteRequests));
    }

    void MqttRequestResponseClientImpl::seatClient(struct aws_mqtt_request_response_client *client) noexcept {
        this->m_client = client;
    }

    void MqttRequestResponseClientImpl::close() noexcept {
        struct aws_linked_list toComplete;
        aws_linked_list_init(&toComplete);

        bool useLock = true;
        struct aws_event_loop *event_loop = aws_mqtt_request_response_client_get_event_loop(m_client);
        if (aws_event_loop_thread_is_callers_thread(event_loop)) {
            useLock = false;
        }

        if (useLock)
        {
            aws_rw_lock_wlock(&m_lock);
        }

        if (!m_closed)
        {
            m_closed = true;
            aws_linked_list_swap_contents(&toComplete, &m_incompleteRequests);
            aws_mqtt_request_response_client_release(m_client);
            m_client = nullptr;
        }

        if (useLock)
        {
            aws_rw_lock_wunlock(&m_lock);
        }

        while (!aws_linked_list_empty(&toComplete)) {
            struct aws_linked_list_node *node = aws_linked_list_pop_front(&toComplete);

            struct IncompleteRequest *incompleteRequest =
                AWS_CONTAINER_OF(node, struct IncompleteRequest, m_node);

            s_completeRequestWithError(incompleteRequest, AWS_ERROR_MQTT_REQUEST_RESPONSE_CLIENT_SHUT_DOWN);
        }
    }

    void MqttRequestResponseClientImpl::onRequestCompletion(struct IncompleteRequest *incompleteRequest, const struct aws_byte_cursor *response_topic,
                             const struct aws_byte_cursor *payload,
                             int error_code) {
        aws_rw_lock_rlock(&m_lock);

        if (!m_closed) {
            if (error_code != AWS_ERROR_SUCCESS) {
                s_completeRequestWithError(incompleteRequest, error_code);
            } else {
                s_completeRequestWithSuccess(incompleteRequest, response_topic, payload);
            }
        }

        aws_rw_lock_runlock(&m_lock);

        aws_ref_count_release(&incompleteRequest->m_refCount);
    }

    int MqttRequestResponseClientImpl::submitRequest(const RequestResponseOperationOptions &requestOptions, IncompleteRequest *incompleteRequest) noexcept
    {
        AWS_FATAL_ASSERT(!requestOptions.subscriptionTopicFilters.empty());
        AWS_FATAL_ASSERT(!requestOptions.responsePaths.empty());

        struct aws_mqtt_request_operation_options rawOptions = {
            struct aws_byte_cursor *subscription_topic_filters;
            size_t subscription_topic_filter_count;
            struct aws_mqtt_request_operation_response_path *response_paths;
            size_t response_path_count;
            struct aws_byte_cursor publish_topic;
            struct aws_byte_cursor serialized_request;
            .correlation_token = ByteCursorFromString(requestOptions.c)
            .completion_callback = s_onRequestComplete,
            .user_data = incompleteRequest
        };

        int result = aws_mqtt_request_response_client_submit_request(m_client, &rawOptions);
        if (result == AWS_OP_ERR) {
            aws_ref_count_release(&incompleteRequest->m_refCount);
            aws_ref_count_release(&incompleteRequest->m_refCount);
        } else {
            aws_linked_list_push_back(&m_incompleteRequests, &incompleteRequest->m_node);
        }

        return result;
    }

    struct ImplHandle {
        Aws::Crt::Allocator *m_allocator;

        std::shared_ptr<MqttRequestResponseClientImpl> m_impl;
    };

    static void s_client_terminated(void *user_data) {
        auto *handle = static_cast<ImplHandle *>(user_data);

        Aws::Crt::Delete(handle, handle->m_allocator);
    }

    MqttRequestResponseClient::~MqttRequestResponseClient() {
        m_impl->close();
    }

    MqttRequestResponseClient *MqttRequestResponseClient::newFrom5(const Aws::Crt::Mqtt5::Mqtt5Client &protocolClient, RequestResponseClientOptions &&options, Aws::Crt::Allocator *allocator) {
        ImplHandle *terminationHandle = Aws::Crt::New<ImplHandle>(allocator);

        struct aws_mqtt_request_response_client_options rrClientOptions = {
            .max_request_response_subscriptions = options.maxRequestResponseSubscriptions,
            .max_streaming_subscriptions = options.maxStreamingSubscriptions,
            .operation_timeout_seconds = options.operationTimeoutInSeconds,
            .terminated_callback = s_client_terminated,
            .user_data = terminationHandle
        };

        struct aws_mqtt_request_response_client *rrClient = aws_mqtt_request_response_client_new_from_mqtt5_client(allocator, protocolClient.GetUnderlyingHandle(), &rrClientOptions);

        auto clientImpl = Aws::Crt::MakeShared<MqttRequestResponseClientImpl>(allocator, allocator);
        terminationHandle->m_impl = clientImpl;

        clientImpl->seatClient(rrClient);

        // Can't use Aws::Crt::New because constructor is private and I don't want to change that
        MqttRequestResponseClient *client = reinterpret_cast<MqttRequestResponseClient *>(aws_mem_acquire(allocator, sizeof(MqttRequestResponseClient)));

        return new (client) MqttRequestResponseClient(allocator, clientImpl);
    }


    MqttRequestResponseClient *MqttRequestResponseClient::newFrom311(const Aws::Crt::Mqtt::MqttConnection &protocolClient, RequestResponseClientOptions &&options, Aws::Crt::Allocator *allocator) {
        return nullptr;
    }

    int MqttRequestResponseClient::submitRequest(const RequestResponseOperationOptions &requestOptions, UnmodeledResultHandler &&resultHandler) {
        struct IncompleteRequest *incompleteRequest = Aws::Crt::New<IncompleteRequest>(m_allocator);
        incompleteRequest->m_allocator = m_allocator;
        incompleteRequest->m_handler = std::move(resultHandler);
        incompleteRequest->m_clientImpl = m_impl;
        AWS_ZERO_STRUCT(incompleteRequest->m_node);
        aws_ref_count_init(&incompleteRequest->m_refCount, incompleteRequest, s_destroyIncompleteRequest);
        aws_ref_count_acquire(&incompleteRequest->m_refCount);

        return m_impl->submitRequest(requestOptions, incompleteRequest);
    }

    MqttRequestResponseClient::MqttRequestResponseClient(Aws::Crt::Allocator *allocator, std::shared_ptr<MqttRequestResponseClientImpl> impl) :
        m_allocator(allocator),
        m_impl(impl)
    {
    }
}
}
}

