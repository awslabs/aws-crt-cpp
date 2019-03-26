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

#include <condition_variable>
#include <mutex>

static int s_TestIotPublishSubscribe(Aws::Crt::Allocator *allocator, void *ctx)
{
    using namespace Aws::Crt;
    using namespace Aws::Crt::Io;
    using namespace Aws::Crt::Mqtt;

    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);
    Aws::Crt::Io::TlsContextOptions tlsCtxOptions =
            Aws::Crt::Io::TlsContextOptions::InitClientWithMtls("/tmp/certificate.pem", "/tmp/privatekey.pem");
    tlsCtxOptions.OverrideDefaultTrustStore(nullptr, "/tmp/AmazonRootCA1.pem");
    Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
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

    auto mqttConnection =
        mqttClient.NewConnection("a16523t7iy5uyg-ats.iot.us-east-1.amazonaws.com", 8883, socketOptions, tlsContext.NewConnectionOptions());

    std::mutex mutex;
    std::condition_variable cv;
    bool connected = false;
    bool subscribed = false;
    bool published = false;
    bool received = false;
    auto onConnectionCompleted = [&](MqttConnection &connection, int errorCode, ReturnCode returnCode, bool sessionPresent) {
        printf("CONNECTED\n");
        connected = true;
        cv.notify_one();
    };
    auto onDisconnect = [&](MqttConnection& connection) {
        printf("DISCONNECTED\n");
        connected = false;
        cv.notify_one();
    };
    auto onTest = [&](MqttConnection& connection, const String& topic, const ByteBuf& payload) {
        printf("GOT MESSAGE @ %s\n", topic.c_str());
        received = true;
        cv.notify_one();
    };
    auto onSubAck = [&](MqttConnection& connection, uint16_t packetId, const String &topic, QOS qos, int errorCode) {
        printf("SUBACK @ %s qos: %d\n", topic.c_str(), qos);
        subscribed = true;
        cv.notify_one();
    };
    auto onPubAck = [&](MqttConnection& connection, uint16_t packetId, int errorCode) {
        printf("PUBLISHED @ %d\n", packetId);
        published = true;
        cv.notify_one();
    };

    mqttConnection->OnConnectionCompleted = onConnectionCompleted;
    mqttConnection->OnDisconnect = onDisconnect;
    mqttConnection->Connect("aws-crt-cpp-v2", true);

    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() { return connected; });
    }

//    mqttConnection->Subscribe("/test/me/senpai", QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, onTest, onSubAck);
//
//    {
//        std::unique_lock<std::mutex> lock(mutex);
//        cv.wait(lock, [&]() { return subscribed; });
//    }
//
//    Aws::Crt::ByteBuf payload = Aws::Crt::ByteBufFromCString("TEST");
//    mqttConnection->Publish("/test/me/senpai", QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, false, payload, onPubAck);
//
//    // wait for publish
//    {
//        std::unique_lock<std::mutex> lock(mutex);
//        cv.wait(lock, [&]() { return published; });
//    }
//
//    // wait for message received callback
//    {
//        std::unique_lock<std::mutex> lock(mutex);
//        cv.wait(lock, [&]() { return received; });
//    }

    mqttConnection->Disconnect();
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() { return !connected; });
    }
    ASSERT_TRUE(mqttConnection);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(IotPublishSubscribe, s_TestIotPublishSubscribe)
