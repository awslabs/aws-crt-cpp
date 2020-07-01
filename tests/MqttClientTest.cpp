/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>

#include <aws/testing/aws_test_harness.h>
#include <utility>

static int s_TestMqttClientResourceSafety(Aws::Crt::Allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);
    Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();

    Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    ASSERT_TRUE(tlsContext);

    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(3000);

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
    ASSERT_TRUE(eventLoopGroup);

    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
    ASSERT_TRUE(defaultHostResolver);

    Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
    ASSERT_TRUE(allocator);
    clientBootstrap.EnableBlockingShutdown();

    Aws::Crt::Mqtt::MqttClient mqttClient(clientBootstrap, allocator);
    ASSERT_TRUE(mqttClient);

    Aws::Crt::Mqtt::MqttClient mqttClientMoved = std::move(mqttClient);
    ASSERT_TRUE(mqttClientMoved);

    auto mqttConnection = mqttClientMoved.NewConnection("www.example.com", 443, socketOptions, tlsContext);

    mqttConnection->SetOnMessageHandler(
        [](Aws::Crt::Mqtt::MqttConnection &, const Aws::Crt::String &, const Aws::Crt::ByteBuf &) {});
    mqttConnection->Disconnect();
    ASSERT_TRUE(mqttConnection);

    // NOLINTNEXTLINE
    ASSERT_FALSE(mqttClient);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(MqttClientResourceSafety, s_TestMqttClientResourceSafety)
