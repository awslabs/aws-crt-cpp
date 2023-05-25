/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Api.h>

#include <aws/testing/aws_test_harness.h>

#include <aws/common/environment.h>
#include <aws/common/string.h>
#include <aws/crt/UUID.h>

#include <condition_variable>
#include <fstream>
#include <mutex>
#include <utility>

#include <aws/io/logging.h>

#if !BYO_CRYPTO

AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_iot_core_host, "AWS_TEST_MQTT311_IOT_CORE_HOST");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_iot_core_cert, "AWS_TEST_MQTT311_IOT_CORE_RSA_CERT");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_iot_core_key, "AWS_TEST_MQTT311_IOT_CORE_RSA_KEY");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_iot_core_ca, "AWS_TEST_MQTT311_ROOT_CA");

static int s_GetEnvVariable(Aws::Crt::Allocator *allocator, const aws_string *variableName, aws_string **output)
{
    int error = aws_get_environment_value(allocator, variableName, output);
    if (error == AWS_OP_SUCCESS && output)
    {
        if (aws_string_is_valid(*output))
        {
            return AWS_OP_SUCCESS;
        }
    }
    return AWS_OP_ERR;
}

static int s_TestIotPublishSubscribe(Aws::Crt::Allocator *allocator, void *ctx)
{
    using namespace Aws::Crt;
    using namespace Aws::Crt::Io;
    using namespace Aws::Crt::Mqtt;

    aws_string *input_host = nullptr;
    aws_string *input_certificate = nullptr;
    aws_string *input_privateKey = nullptr;
    aws_string *input_rootCa = nullptr;
    int envResult = s_GetEnvVariable(allocator, s_mqtt311_test_envName_iot_core_host, &input_host);
    envResult |= s_GetEnvVariable(allocator, s_mqtt311_test_envName_iot_core_cert, &input_certificate);
    envResult |= s_GetEnvVariable(allocator, s_mqtt311_test_envName_iot_core_key, &input_privateKey);
    envResult |= s_GetEnvVariable(allocator, s_mqtt311_test_envName_iot_core_ca, &input_rootCa);
    if (envResult != AWS_OP_SUCCESS)
    {
        printf("Required environment variable is not set or missing. Skipping test\n");
        aws_string_destroy(input_host);
        aws_string_destroy(input_certificate);
        aws_string_destroy(input_privateKey);
        aws_string_destroy(input_rootCa);
        return AWS_OP_SKIP;
    }

    const char *credentialFiles[] = {
        aws_string_c_str(input_certificate), aws_string_c_str(input_privateKey), aws_string_c_str(input_rootCa)};

    for (size_t fileIdx = 0; fileIdx < AWS_ARRAY_SIZE(credentialFiles); ++fileIdx)
    {
        std::ifstream file;
        file.open(credentialFiles[fileIdx]);
        if (!file.is_open())
        {
            printf("Required credential file %s is missing or unreadable, skipping test\n", credentialFiles[fileIdx]);
            aws_string_destroy(input_host);
            aws_string_destroy(input_certificate);
            aws_string_destroy(input_privateKey);
            aws_string_destroy(input_rootCa);
            return AWS_OP_SKIP;
        }
    }

    (void)ctx;
    {
        Aws::Crt::ApiHandle apiHandle(allocator);

        Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitClientWithMtls(
            aws_string_c_str(input_certificate), aws_string_c_str(input_privateKey));
        tlsCtxOptions.OverrideDefaultTrustStore(nullptr, aws_string_c_str(input_rootCa));
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
                mqttClient.NewConnection(aws_string_c_str(input_host), 8883, socketOptions, tlsContext);

            std::mutex mutex;
            std::condition_variable cv;
            bool connected = false;
            bool subscribed = false;
            bool published = false;
            bool received = false;
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

            mqttConnection->OnConnectionCompleted = onConnectionCompleted;
            mqttConnection->OnDisconnect = onDisconnect;
            Aws::Crt::UUID Uuid;
            Aws::Crt::String uuidStr = Uuid.ToString();
            mqttConnection->Connect(uuidStr.c_str(), true);

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
    }

    aws_string_destroy(input_host);
    aws_string_destroy(input_certificate);
    aws_string_destroy(input_privateKey);
    aws_string_destroy(input_rootCa);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(IotPublishSubscribe, s_TestIotPublishSubscribe)

static int s_TestIotWillTest(Aws::Crt::Allocator *allocator, void *ctx)
{
    using namespace Aws::Crt;
    using namespace Aws::Crt::Io;
    using namespace Aws::Crt::Mqtt;

    aws_string *input_host = nullptr;
    aws_string *input_certificate = nullptr;
    aws_string *input_privateKey = nullptr;
    aws_string *input_rootCa = nullptr;
    int envResult = s_GetEnvVariable(allocator, s_mqtt311_test_envName_iot_core_host, &input_host);
    envResult |= s_GetEnvVariable(allocator, s_mqtt311_test_envName_iot_core_cert, &input_certificate);
    envResult |= s_GetEnvVariable(allocator, s_mqtt311_test_envName_iot_core_key, &input_privateKey);
    envResult |= s_GetEnvVariable(allocator, s_mqtt311_test_envName_iot_core_ca, &input_rootCa);
    if (envResult != AWS_OP_SUCCESS)
    {
        printf("Required environment variable is not set or missing. Skipping test\n");
        aws_string_destroy(input_host);
        aws_string_destroy(input_certificate);
        aws_string_destroy(input_privateKey);
        aws_string_destroy(input_rootCa);
        return AWS_OP_SKIP;
    }

    const char *credentialFiles[] = {
        aws_string_c_str(input_certificate), aws_string_c_str(input_privateKey), aws_string_c_str(input_rootCa)};

    for (size_t fileIdx = 0; fileIdx < AWS_ARRAY_SIZE(credentialFiles); ++fileIdx)
    {
        std::ifstream file;
        file.open(credentialFiles[fileIdx]);
        if (!file.is_open())
        {
            printf("Required credential file %s is missing or unreadable, skipping test\n", credentialFiles[fileIdx]);
            aws_string_destroy(input_host);
            aws_string_destroy(input_certificate);
            aws_string_destroy(input_privateKey);
            aws_string_destroy(input_rootCa);
            return AWS_OP_SKIP;
        }
    }

    (void)ctx;
    {
        Aws::Crt::ApiHandle apiHandle(allocator);

        Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitClientWithMtls(
            aws_string_c_str(input_certificate), aws_string_c_str(input_privateKey));
        tlsCtxOptions.OverrideDefaultTrustStore(nullptr, aws_string_c_str(input_rootCa));
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

        Aws::Crt::UUID Uuid;
        Aws::Crt::String uuidStr = Uuid.ToString();

        Aws::Crt::String topicStr = "will/topic/";
        topicStr += uuidStr;
        Aws::Crt::ByteBuf payload = Aws::Crt::ByteBufFromCString("notice me pls");

        auto willConnection = mqttClient.NewConnection(aws_string_c_str(input_host), 8883, socketOptions, tlsContext);
        willConnection->SetWill(topicStr.c_str(), QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, false, payload);
        std::mutex willMutex;
        std::condition_variable willCv;
        bool willConnected = false;
        auto willOnConnectionCompleted =
            [&](MqttConnection &, int errorCode, ReturnCode returnCode, bool sessionPresent) {
                (void)errorCode;
                (void)returnCode;
                (void)sessionPresent;
                willConnected = true;
                willCv.notify_one();
            };
        auto willOnDisconnect = [&](MqttConnection &) {
            willConnected = false;
            willCv.notify_one();
        };
        willConnection->OnConnectionCompleted = willOnConnectionCompleted;
        willConnection->OnDisconnect = willOnDisconnect;
        willConnection->Connect((Aws::Crt::String("test-01-") + uuidStr).c_str(), true);
        {
            std::unique_lock<std::mutex> lock(willMutex);
            willCv.wait(lock, [&]() { return willConnected; });
        }

        auto subscriberConnection =
            mqttClient.NewConnection(aws_string_c_str(input_host), 8883, socketOptions, tlsContext);
        std::mutex subscriberMutex;
        std::condition_variable subscriberCv;
        bool subscriberConnected = false;
        bool subscriberSubscribed = false;
        bool subscriberReceived = false;
        auto subscriberOnConnectionCompleted =
            [&](MqttConnection &, int errorCode, ReturnCode returnCode, bool sessionPresent) {
                (void)errorCode;
                (void)returnCode;
                (void)sessionPresent;
                subscriberConnected = true;
                subscriberCv.notify_one();
            };
        auto subscriberOnDisconnect = [&](MqttConnection &) {
            subscriberConnected = false;
            subscriberCv.notify_one();
        };
        auto subscriberOnSubAck = [&](MqttConnection &, uint16_t packetId, const String &topic, QOS qos, int) {
            (void)packetId;
            (void)topic;
            (void)qos;
            subscriberSubscribed = true;
            subscriberCv.notify_one();
        };
        auto subscriberOnTest = [&](MqttConnection &, const String &topic, const ByteBuf &payload) {
            (void)topic;
            (void)payload;
            subscriberReceived = true;
            subscriberCv.notify_one();
        };
        subscriberConnection->OnConnectionCompleted = subscriberOnConnectionCompleted;
        subscriberConnection->OnDisconnect = subscriberOnDisconnect;
        subscriberConnection->Connect((Aws::Crt::String("test-02-") + uuidStr).c_str(), true);
        {
            std::unique_lock<std::mutex> lock(subscriberMutex);
            subscriberCv.wait(lock, [&]() { return subscriberConnected; });
        }
        subscriberConnection->Subscribe(
            topicStr.c_str(), QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, subscriberOnTest, subscriberOnSubAck);
        {
            std::unique_lock<std::mutex> lock(subscriberMutex);
            subscriberCv.wait(lock, [&]() { return subscriberSubscribed; });
        }

        // Disconnect the client by interrupting it with another client with the same ID
        // which will cause the will to be sent
        auto interruptConnection =
            mqttClient.NewConnection(aws_string_c_str(input_host), 8883, socketOptions, tlsContext);
        interruptConnection->SetWill(topicStr.c_str(), QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, false, payload);
        std::mutex interruptMutex;
        std::condition_variable interruptCv;
        bool interruptConnected = false;
        auto interruptOnConnectionCompleted =
            [&](MqttConnection &, int errorCode, ReturnCode returnCode, bool sessionPresent) {
                (void)errorCode;
                (void)returnCode;
                (void)sessionPresent;
                interruptConnected = true;
                interruptCv.notify_one();
            };
        auto interruptOnDisconnect = [&](MqttConnection &) {
            interruptConnected = false;
            interruptCv.notify_one();
        };
        interruptConnection->OnConnectionCompleted = interruptOnConnectionCompleted;
        interruptConnection->OnDisconnect = interruptOnDisconnect;
        interruptConnection->Connect((Aws::Crt::String("test-01-") + uuidStr).c_str(), true);
        {
            std::unique_lock<std::mutex> lock(interruptMutex);
            interruptCv.wait(lock, [&]() { return interruptConnected; });
        }

        // wait for message received callback - meaning the will was sent
        {
            std::unique_lock<std::mutex> lock(subscriberMutex);
            subscriberCv.wait(lock, [&]() { return subscriberReceived; });
        }

        // Disconnect everything
        willConnection->Disconnect();
        {
            std::unique_lock<std::mutex> lock(willMutex);
            willCv.wait(lock, [&]() { return !willConnected; });
        }
        interruptConnection->Disconnect();
        {
            std::unique_lock<std::mutex> lock(interruptMutex);
            interruptCv.wait(lock, [&]() { return !interruptConnected; });
        }
        subscriberConnection->Disconnect();
        {
            std::unique_lock<std::mutex> lock(subscriberMutex);
            subscriberCv.wait(lock, [&]() { return !subscriberConnected; });
        }
    }

    aws_string_destroy(input_host);
    aws_string_destroy(input_certificate);
    aws_string_destroy(input_privateKey);
    aws_string_destroy(input_rootCa);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(IotWillTest, s_TestIotWillTest)

static int s_TestIotStatisticsPublishWaitStatisticsDisconnect(Aws::Crt::Allocator *allocator, void *ctx)
{
    using namespace Aws::Crt;
    using namespace Aws::Crt::Io;
    using namespace Aws::Crt::Mqtt;

    aws_string *input_host = nullptr;
    aws_string *input_certificate = nullptr;
    aws_string *input_privateKey = nullptr;
    aws_string *input_rootCa = nullptr;
    int envResult = s_GetEnvVariable(allocator, s_mqtt311_test_envName_iot_core_host, &input_host);
    envResult |= s_GetEnvVariable(allocator, s_mqtt311_test_envName_iot_core_cert, &input_certificate);
    envResult |= s_GetEnvVariable(allocator, s_mqtt311_test_envName_iot_core_key, &input_privateKey);
    envResult |= s_GetEnvVariable(allocator, s_mqtt311_test_envName_iot_core_ca, &input_rootCa);
    if (envResult != AWS_OP_SUCCESS)
    {
        printf("Required environment variable is not set or missing. Skipping test\n");
        aws_string_destroy(input_host);
        aws_string_destroy(input_certificate);
        aws_string_destroy(input_privateKey);
        aws_string_destroy(input_rootCa);
        return AWS_OP_SKIP;
    }

    const char *credentialFiles[] = {
        aws_string_c_str(input_certificate), aws_string_c_str(input_privateKey), aws_string_c_str(input_rootCa)};

    for (size_t fileIdx = 0; fileIdx < AWS_ARRAY_SIZE(credentialFiles); ++fileIdx)
    {
        std::ifstream file;
        file.open(credentialFiles[fileIdx]);
        if (!file.is_open())
        {
            printf("Required credential file %s is missing or unreadable, skipping test\n", credentialFiles[fileIdx]);
            aws_string_destroy(input_host);
            aws_string_destroy(input_certificate);
            aws_string_destroy(input_privateKey);
            aws_string_destroy(input_rootCa);
            return AWS_OP_SKIP;
        }
    }

    (void)ctx;
    {
        Aws::Crt::ApiHandle apiHandle(allocator);

        Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitClientWithMtls(
            aws_string_c_str(input_certificate), aws_string_c_str(input_privateKey));
        tlsCtxOptions.OverrideDefaultTrustStore(nullptr, aws_string_c_str(input_rootCa));
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

        auto mqttConnection = mqttClient.NewConnection(aws_string_c_str(input_host), 8883, socketOptions, tlsContext);

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
        Aws::Crt::UUID Uuid;
        Aws::Crt::String uuidStr = Uuid.ToString();
        mqttConnection->Connect(uuidStr.c_str(), true);

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

    aws_string_destroy(input_host);
    aws_string_destroy(input_certificate);
    aws_string_destroy(input_privateKey);
    aws_string_destroy(input_rootCa);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(IoTStatisticsPublishWaitStatisticsDisconnect, s_TestIotStatisticsPublishWaitStatisticsDisconnect)

static int s_TestIotStatisticsPublishStatisticsWaitDisconnect(Aws::Crt::Allocator *allocator, void *ctx)
{
    using namespace Aws::Crt;
    using namespace Aws::Crt::Io;
    using namespace Aws::Crt::Mqtt;

    aws_string *input_host = nullptr;
    aws_string *input_certificate = nullptr;
    aws_string *input_privateKey = nullptr;
    aws_string *input_rootCa = nullptr;
    int envResult = s_GetEnvVariable(allocator, s_mqtt311_test_envName_iot_core_host, &input_host);
    envResult |= s_GetEnvVariable(allocator, s_mqtt311_test_envName_iot_core_cert, &input_certificate);
    envResult |= s_GetEnvVariable(allocator, s_mqtt311_test_envName_iot_core_key, &input_privateKey);
    envResult |= s_GetEnvVariable(allocator, s_mqtt311_test_envName_iot_core_ca, &input_rootCa);
    if (envResult != AWS_OP_SUCCESS)
    {
        printf("Required environment variable is not set or missing. Skipping test\n");
        aws_string_destroy(input_host);
        aws_string_destroy(input_certificate);
        aws_string_destroy(input_privateKey);
        aws_string_destroy(input_rootCa);
        return AWS_OP_SKIP;
    }

    const char *credentialFiles[] = {
        aws_string_c_str(input_certificate), aws_string_c_str(input_privateKey), aws_string_c_str(input_rootCa)};

    for (size_t fileIdx = 0; fileIdx < AWS_ARRAY_SIZE(credentialFiles); ++fileIdx)
    {
        std::ifstream file;
        file.open(credentialFiles[fileIdx]);
        if (!file.is_open())
        {
            printf("Required credential file %s is missing or unreadable, skipping test\n", credentialFiles[fileIdx]);
            aws_string_destroy(input_host);
            aws_string_destroy(input_certificate);
            aws_string_destroy(input_privateKey);
            aws_string_destroy(input_rootCa);
            return AWS_OP_SKIP;
        }
    }

    (void)ctx;
    {
        Aws::Crt::ApiHandle apiHandle(allocator);

        Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitClientWithMtls(
            aws_string_c_str(input_certificate), aws_string_c_str(input_privateKey));
        tlsCtxOptions.OverrideDefaultTrustStore(nullptr, aws_string_c_str(input_rootCa));
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

        auto mqttConnection = mqttClient.NewConnection(aws_string_c_str(input_host), 8883, socketOptions, tlsContext);

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
        Aws::Crt::UUID Uuid;
        Aws::Crt::String uuidStr = Uuid.ToString();
        mqttConnection->Connect(uuidStr.c_str(), true);
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
        // Per packet: (The size of the topic (18), the size of the payload, 2 for the header and 2 for the packet ID)
        uint64_t expected_size = payload.len + 22;
        statistics = mqttConnection->GetOperationStatistics();
        ASSERT_INT_EQUALS(1, statistics.incompleteOperationCount);
        ASSERT_INT_EQUALS(expected_size, statistics.incompleteOperationSize);

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

    aws_string_destroy(input_host);
    aws_string_destroy(input_certificate);
    aws_string_destroy(input_privateKey);
    aws_string_destroy(input_rootCa);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(IoTStatisticsPublishStatisticsWaitDisconnect, s_TestIotStatisticsPublishStatisticsWaitDisconnect)

#endif // !BYO_CRYPTO
