/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Api.h>

#include <aws/testing/aws_test_harness.h>
#include <utility>

#include <condition_variable>
#include <fstream>
#include <mutex>

#include <aws/io/logging.h>

#define TEST_CERTIFICATE "/tmp/certificate.pem"
#define TEST_PRIVATEKEY "/tmp/privatekey.pem"
#define TEST_ROOTCA "/tmp/AmazonRootCA1.pem"

static int s_TestIotPublishSubscribe(Aws::Crt::Allocator *allocator, void *ctx)
{
    using namespace Aws::Crt;
    using namespace Aws::Crt::Io;
    using namespace Aws::Crt::Mqtt;

    const char *credentialFiles[] = {TEST_CERTIFICATE, TEST_PRIVATEKEY, TEST_ROOTCA};

    for (int fileIdx = 0; fileIdx < AWS_ARRAY_SIZE(credentialFiles); ++fileIdx)
    {
        std::ifstream file;
        file.open(credentialFiles[fileIdx]);
        if (!file.is_open())
        {
            printf("Required credential file %s is missing or unreadable, skipping test\n", credentialFiles[fileIdx]);
            return AWS_ERROR_SUCCESS;
        }
    }

    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::Io::TlsContextOptions tlsCtxOptions =
        Aws::Crt::Io::TlsContextOptions::InitClientWithMtls(TEST_CERTIFICATE, TEST_PRIVATEKEY);
    tlsCtxOptions.OverrideDefaultTrustStore(nullptr, TEST_ROOTCA);
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

    int tries = 0;
    while (tries++ < 10)
    {
        auto mqttConnection =
            mqttClient.NewConnection("a16523t7iy5uyg-ats.iot.us-east-1.amazonaws.com", 8883, socketOptions, tlsContext);

        std::mutex mutex;
        std::condition_variable cv;
        bool connected = false;
        bool subscribed = false;
        bool published = false;
        bool received = false;
        auto onConnectionCompleted =
            [&](MqttConnection &connection, int errorCode, ReturnCode returnCode, bool sessionPresent) {
                printf(
                    "%s errorCode=%d returnCode=%d sessionPresent=%d\n",
                    (errorCode == 0) ? "CONNECTED" : "COMPLETED",
                    errorCode,
                    (int)returnCode,
                    (int)sessionPresent);
                connected = true;
                cv.notify_one();
            };
        auto onDisconnect = [&](MqttConnection &connection) {
            printf("DISCONNECTED\n");
            connected = false;
            cv.notify_one();
        };
        auto onTest = [&](MqttConnection &connection, const String &topic, const ByteBuf &payload) {
            printf("GOT MESSAGE topic=%s payload=" PRInSTR "\n", topic.c_str(), AWS_BYTE_BUF_PRI(payload));
            received = true;
            cv.notify_one();
        };
        auto onSubAck =
            [&](MqttConnection &connection, uint16_t packetId, const String &topic, QOS qos, int errorCode) {
                printf("SUBACK id=%d topic=%s qos=%d\n", packetId, topic.c_str(), qos);
                subscribed = true;
                cv.notify_one();
            };
        auto onPubAck = [&](MqttConnection &connection, uint16_t packetId, int errorCode) {
            printf("PUBLISHED id=%d\n", packetId);
            published = true;
            cv.notify_one();
        };

        mqttConnection->OnConnectionCompleted = onConnectionCompleted;
        mqttConnection->OnDisconnect = onDisconnect;
        char clientId[32];
        snprintf(clientId, sizeof(clientId), "aws-crt-cpp-v2-%d", tries);
        mqttConnection->Connect(clientId, true);

        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() { return connected; });
        }

        mqttConnection->Subscribe("/publish/me/senpai", QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, onTest, onSubAck);

        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() { return subscribed; });
        }

        Aws::Crt::ByteBuf payload = Aws::Crt::ByteBufFromCString("notice me pls");
        mqttConnection->Publish("/publish/me/senpai", QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, false, payload, onPubAck);

        // wait for publish
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() { return published; });
        }

        // wait for message received callback
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() { return received; });
        }

        mqttConnection->Disconnect();
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() { return !connected; });
        }
        ASSERT_TRUE(mqttConnection);
    }

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(IotPublishSubscribe, s_TestIotPublishSubscribe)
