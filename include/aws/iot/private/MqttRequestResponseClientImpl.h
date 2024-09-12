#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Exports.h>

#include <aws/common/linked_list.h>
#include <aws/common/rw_lock.h>
#include <aws/iot/MqttRequestResponseClient.h>

struct aws_allocator;
struct aws_mqtt_request_response_client;

namespace Aws
{
namespace Iot
{
namespace RequestResponse
{

    struct IncompleteRequest;

    class AWS_CRT_CPP_API MqttRequestResponseClientImpl
    {
      public:
        MqttRequestResponseClientImpl(Aws::Crt::Allocator *allocator) noexcept;
        ~MqttRequestResponseClientImpl();

        void seatClient(struct aws_mqtt_request_response_client *client) noexcept;

        void close() noexcept;

        int submitRequest(
            const RequestResponseOperationOptions &requestOptions,
            IncompleteRequest *incompleteRequest) noexcept;

        void onRequestCompletion(struct IncompleteRequest *incompleteRequest, const struct aws_byte_cursor *response_topic,
                                 const struct aws_byte_cursor *payload,
                                 int error_code);

      private:

        Aws::Crt::Allocator *m_allocator;

        struct aws_event_loop *protocolClientLoop;

        struct aws_rw_lock m_lock;

        struct aws_mqtt_request_response_client *m_client;

        struct aws_linked_list m_incompleteRequests;

        bool m_closed;
    };

}
}
}