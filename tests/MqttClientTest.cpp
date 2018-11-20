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
    Aws::Crt::Io::TlsContextOptions tlsCtxOptions;
    Aws::Crt::Io::InitDefaultClient(tlsCtxOptions);

    Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TLSMode::CLIENT, allocator);
    ASSERT_TRUE(tlsContext);

    Aws::Crt::Io::SocketOptions socketOptions;
    AWS_ZERO_STRUCT(socketOptions);
    socketOptions.type = AWS_SOCKET_STREAM;
    socketOptions.domain = AWS_SOCKET_IPV4;
    socketOptions.connect_timeout_ms = 3000;

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
    ASSERT_TRUE(eventLoopGroup);

    Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, allocator);
    ASSERT_TRUE(allocator);

    Aws::Crt::Mqtt::MqttClient mqttClient(clientBootstrap, allocator);
    ASSERT_TRUE(mqttClient);

    //Uncomment the next section once connection clean up code in the underlying c lib has been updated.
    //Aws::Crt::Mqtt::MqttConnection mqttConnection = mqttClient.NewConnection("www.example.com", 443,
    //    socketOptions, tlsContext.NewConnectionOptions());
    //mqttConnection.Disconnect();
    //
    //ASSERT_TRUE(mqttConnection);

    Aws::Crt::Mqtt::MqttClient mqttClientMoved = std::move(mqttClient);
    ASSERT_TRUE(mqttClientMoved);

    // NOLINTNEXTLINE
    ASSERT_FALSE(mqttClient);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(MqttClientResourceSafety, s_TestMqttClientResourceSafety)
