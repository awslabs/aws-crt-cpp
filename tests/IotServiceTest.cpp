/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Api.h>

#include <aws/testing/aws_test_harness.h>

#include <condition_variable>
#include <fstream>
#include <mutex>
#include <utility>

#include <aws/io/logging.h>

#define TEST_CERTIFICATE "/tmp/certificate.pem"
#define TEST_PRIVATEKEY "/tmp/privatekey.pem"
#define TEST_ROOTCA "/tmp/AmazonRootCA1.pem"

#if !BYO_CRYPTO

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
    {
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
            auto mqttConnection = mqttClient.NewConnection(
                "a16523t7iy5uyg-ats.iot.us-east-1.amazonaws.com", 8883, socketOptions, tlsContext);

            std::mutex mutex;
            std::condition_variable cv;
            bool connected = false;
            bool subscribed = false;
            bool published = false;
            bool received = false;
            bool connection_success = false;
            bool closed = false;
            auto onConnectionCompleted =
                [&](MqttConnection &, int errorCode, ReturnCode returnCode, bool sessionPresent) {
                    printf(
                        "%s errorCode=%d returnCode=%d sessionPresent=%d\n",
                        (errorCode == 0) ? "CONNECTED" : "COMPLETED",
                        errorCode,
                        (int)returnCode,
                        (int)sessionPresent);
                    connected = true;
                    cv.notify_one();
                };
            auto onDisconnect = [&](MqttConnection &) {
                printf("DISCONNECTED\n");
                connected = false;
                cv.notify_one();
            };
            auto onTest = [&](MqttConnection &, const String &topic, const ByteBuf &payload) {
                printf("GOT MESSAGE topic=%s payload=" PRInSTR "\n", topic.c_str(), AWS_BYTE_BUF_PRI(payload));
                received = true;
                cv.notify_one();
            };
            auto onSubAck = [&](MqttConnection &, uint16_t packetId, const String &topic, QOS qos, int) {
                printf("SUBACK id=%d topic=%s qos=%d\n", packetId, topic.c_str(), qos);
                subscribed = true;
                cv.notify_one();
            };
            auto onPubAck = [&](MqttConnection &, uint16_t packetId, int) {
                printf("PUBLISHED id=%d\n", packetId);
                published = true;
                cv.notify_one();
            };
            auto onConnectionSuccess = [&](MqttConnection &, OnConnectionSuccessData *data) {
                connection_success = true;
                printf("CONNECTION SUCCESS: returnCode=%i sessionPresent=%i\n", data->returnCode, data->sessionPresent);
                cv.notify_one();
            };
            auto onConnectionClosed = [&](MqttConnection &, OnConnectionClosedData *data) {
                closed = true;
                cv.notify_one();
            };

            mqttConnection->OnConnectionCompleted = onConnectionCompleted;
            mqttConnection->OnDisconnect = onDisconnect;
            mqttConnection->OnConnectionSuccess = onConnectionSuccess;
            mqttConnection->OnConnectionClosed = onConnectionClosed;
            char clientId[32];
            snprintf(clientId, sizeof(clientId), "aws-crt-cpp-v2-%d", tries);
            mqttConnection->Connect(clientId, true);

            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&]() { return connected; });
            }

            // Make sure connection success callback fired
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&]() { return connection_success; });
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

            // Make sure closed callback fired
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&]() { return closed; });
            }
            ASSERT_TRUE(mqttConnection);
        }
    }

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(IotPublishSubscribe, s_TestIotPublishSubscribe)

static int s_TestIotFailTest(Aws::Crt::Allocator *allocator, void *ctx)
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
    {
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
            // Intentially use a bad port so we fail to connect
            auto mqttConnection = mqttClient.NewConnection(
                "a16523t7iy5uyg-ats.iot.us-east-1.amazonaws.com", 123, socketOptions, tlsContext);

            std::mutex mutex;
            std::condition_variable cv;
            bool connected = false;
            bool connection_failure = false;
            bool closed = false;
            auto onConnectionCompleted =
                [&](MqttConnection &, int errorCode, ReturnCode returnCode, bool sessionPresent) {
                    printf(
                        "%s errorCode=%d returnCode=%d sessionPresent=%d\n",
                        (errorCode == 0) ? "CONNECTED" : "COMPLETED",
                        errorCode,
                        (int)returnCode,
                        (int)sessionPresent);
                    connected = true;
                    cv.notify_one();
                };
            auto onDisconnect = [&](MqttConnection &) {
                printf("DISCONNECTED\n");
                connected = false;
                cv.notify_one();
            };
            auto onConnectionFailure = [&](MqttConnection &, OnConnectionFailureData *data) {
                connection_failure = true;
                printf("CONNECTION FAILURE: error=%i\n", data->error);
                cv.notify_one();
            };
            auto onConnectionClosed = [&](MqttConnection &, OnConnectionClosedData *data) {
                closed = true;
                cv.notify_one();
            };

            mqttConnection->OnConnectionCompleted = onConnectionCompleted;
            mqttConnection->OnDisconnect = onDisconnect;
            mqttConnection->OnConnectionFailure = onConnectionFailure;
            mqttConnection->OnConnectionClosed = onConnectionClosed;
            char clientId[32];
            snprintf(clientId, sizeof(clientId), "aws-crt-cpp-v2-%d", tries);
            mqttConnection->Connect(clientId, true);

            // Make sure the connection failure callback fired
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&]() { return connection_failure; });
            }

            mqttConnection->Disconnect();
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&]() { return !connected; });
            }

            // Make sure closed callback fired
            {
                std::unique_lock<std::mutex> lock(mutex);
                cv.wait(lock, [&]() { return closed; });
            }
            ASSERT_TRUE(mqttConnection);
        }
    }

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(IotFailTest, s_TestIotFailTest)

static int s_TestIotStatisticsPublishWaitStatisticsDisconnect(Aws::Crt::Allocator *allocator, void *ctx)
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
    {
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

        auto mqttConnection =
            mqttClient.NewConnection("a16523t7iy5uyg-ats.iot.us-east-1.amazonaws.com", 8883, socketOptions, tlsContext);

        std::mutex mutex;
        std::condition_variable cv;
        bool connected = false;
        bool published = false;
        auto onConnectionCompleted = [&](MqttConnection &, int errorCode, ReturnCode returnCode, bool sessionPresent) {
            printf(
                "%s errorCode=%d returnCode=%d sessionPresent=%d\n",
                (errorCode == 0) ? "CONNECTED" : "COMPLETED",
                errorCode,
                (int)returnCode,
                (int)sessionPresent);
            connected = true;
            cv.notify_one();
        };
        auto onDisconnect = [&](MqttConnection &) {
            printf("DISCONNECTED\n");
            connected = false;
            cv.notify_one();
        };
        auto onPubAck = [&](MqttConnection &, uint16_t packetId, int) {
            printf("PUBLISHED id=%d\n", packetId);
            published = true;
            cv.notify_one();
        };

        mqttConnection->OnConnectionCompleted = onConnectionCompleted;
        mqttConnection->OnDisconnect = onDisconnect;
        char clientId[32];
        snprintf(clientId, sizeof(clientId), "aws-crt-cpp-v2-test");
        mqttConnection->Connect(clientId, true);

        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() { return connected; });
        }

        // Check operation statistics
        Aws::Crt::Mqtt::MqttConnectionOperationStatistics statistics = mqttConnection->GetOperationStatistics();
        ASSERT_INT_EQUALS(0, statistics.incompleteOperationCount);
        ASSERT_INT_EQUALS(0, statistics.incompleteOperationSize);
        ASSERT_INT_EQUALS(0, statistics.unackedOperationCount);
        ASSERT_INT_EQUALS(0, statistics.unackedOperationSize);

        Aws::Crt::ByteBuf payload = Aws::Crt::ByteBufFromCString("notice me pls");
        mqttConnection->Publish("/publish/me/senpai", QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, false, payload, onPubAck);

        // wait for publish
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() { return published; });
        }

        // Check operation statistics
        statistics = mqttConnection->GetOperationStatistics();
        ASSERT_INT_EQUALS(0, statistics.incompleteOperationCount);
        ASSERT_INT_EQUALS(0, statistics.incompleteOperationSize);
        ASSERT_INT_EQUALS(0, statistics.unackedOperationCount);
        ASSERT_INT_EQUALS(0, statistics.unackedOperationSize);

        mqttConnection->Disconnect();
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() { return !connected; });
        }
        ASSERT_TRUE(mqttConnection);
    }

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(IoTStatisticsPublishWaitStatisticsDisconnect, s_TestIotStatisticsPublishWaitStatisticsDisconnect)

static int s_TestIotStatisticsPublishStatisticsWaitDisconnect(Aws::Crt::Allocator *allocator, void *ctx)
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
    {
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

        auto mqttConnection =
            mqttClient.NewConnection("a16523t7iy5uyg-ats.iot.us-east-1.amazonaws.com", 8883, socketOptions, tlsContext);

        std::mutex mutex;
        std::condition_variable cv;
        bool connected = false;
        bool published = false;
        auto onConnectionCompleted = [&](MqttConnection &, int errorCode, ReturnCode returnCode, bool sessionPresent) {
            printf(
                "%s errorCode=%d returnCode=%d sessionPresent=%d\n",
                (errorCode == 0) ? "CONNECTED" : "COMPLETED",
                errorCode,
                (int)returnCode,
                (int)sessionPresent);
            connected = true;
            cv.notify_one();
        };
        auto onDisconnect = [&](MqttConnection &) {
            printf("DISCONNECTED\n");
            connected = false;
            cv.notify_one();
        };
        auto onPubAck = [&](MqttConnection &, uint16_t packetId, int) {
            printf("PUBLISHED id=%d\n", packetId);
            published = true;
            cv.notify_one();
        };

        mqttConnection->OnConnectionCompleted = onConnectionCompleted;
        mqttConnection->OnDisconnect = onDisconnect;
        char clientId[32];
        snprintf(clientId, sizeof(clientId), "aws-crt-cpp-v2-test2");
        mqttConnection->Connect(clientId, true);

        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() { return connected; });
        }

        // Check operation statistics
        Aws::Crt::Mqtt::MqttConnectionOperationStatistics statistics = mqttConnection->GetOperationStatistics();
        ASSERT_INT_EQUALS(0, statistics.incompleteOperationCount);
        ASSERT_INT_EQUALS(0, statistics.incompleteOperationSize);
        ASSERT_INT_EQUALS(0, statistics.unackedOperationCount);
        ASSERT_INT_EQUALS(0, statistics.unackedOperationSize);

        Aws::Crt::ByteBuf payload = Aws::Crt::ByteBufFromCString("notice me pls");
        mqttConnection->Publish("/publish/me/senpai", QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, false, payload, onPubAck);

        // Check operation statistics
        // Per packet: (The size of the topic (19), the size of the payload, 2 for the header and 2 for the packet ID)
        uint64_t expected_size = payload.len + 23;
        statistics = mqttConnection->GetOperationStatistics();
        ASSERT_INT_EQUALS(1, statistics.incompleteOperationCount);
        ASSERT_INT_EQUALS(expected_size, statistics.incompleteOperationSize);
        // NOTE: Unacked will be zero because we have not invoked the future yet and so it has not had time to move to
        // the socket
        ASSERT_INT_EQUALS(0, statistics.unackedOperationCount);
        ASSERT_INT_EQUALS(0, statistics.unackedOperationSize);

        // wait for publish
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() { return published; });
        }

        // Check operation statistics
        statistics = mqttConnection->GetOperationStatistics();
        ASSERT_INT_EQUALS(0, statistics.incompleteOperationCount);
        ASSERT_INT_EQUALS(0, statistics.incompleteOperationSize);
        ASSERT_INT_EQUALS(0, statistics.unackedOperationCount);
        ASSERT_INT_EQUALS(0, statistics.unackedOperationSize);

        mqttConnection->Disconnect();
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [&]() { return !connected; });
        }
        ASSERT_TRUE(mqttConnection);
    }

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(IoTStatisticsPublishStatisticsWaitDisconnect, s_TestIotStatisticsPublishStatisticsWaitDisconnect)

#endif // !BYO_CRYPTO
