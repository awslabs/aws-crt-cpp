/*
 * Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
#include <aws/crt/Api.h>

#include <aws/testing/aws_test_harness.h>
#include <utility>

static int s_TestMqttClientResourceSafety(Aws::Crt::Allocator* allocator, void *)
{
    Aws::Crt::ApiHandle apiHandle(allocator);
    Aws::Crt::Io::TLSCtxOptions tlsCtxOptions;
    Aws::Crt::Io::InitDefaultClient(tlsCtxOptions);

    Aws::Crt::Io::SocketOptions socketOptions;
    AWS_ZERO_STRUCT(socketOptions);
    socketOptions.type = AWS_SOCKET_STREAM;
    socketOptions.domain = AWS_SOCKET_IPV4;
    socketOptions.connect_timeout_ms = 3000;

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(allocator, 0);
    ASSERT_TRUE(eventLoopGroup);

    Aws::Crt::Mqtt::MqttClient mqttClient(*allocator, eventLoopGroup);
    ASSERT_TRUE(mqttClient);

    /* uncomment this once the connection clean-up stuff has been properly added to the client
    Aws::Crt::Mqtt::MqttConnection mqttConnection = mqttClient.NewConnection("www.example.com", 443,
        socketOptions, tlsCtxOptions);
    mqttConnection.Disconnect();

    ASSERT_TRUE(mqttConnection);*/

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(MqttClientResourceSafety, s_TestMqttClientResourceSafety)
