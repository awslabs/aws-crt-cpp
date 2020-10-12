/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>

#include <Utils.h>
#include <aws/crt/iot/DeviceDefender.h>
#include <aws/testing/aws_test_harness.h>
#include <utility>

static int s_TestDeviceDefenderResourceSafety(Aws::Crt::Allocator *allocator, void *ctx)
{
    (void)ctx;
    {
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

        Aws::Crt::String data("TestData");

        // std::mutex mutex;
        // std::condition_variable cv;

        Aws::Crt::Iot::DeviceDefenderV1ReportTaskConfig taskConfig(
            mqttConnection,
            Aws::Crt::ByteCursorFromCString("TestThing"),
            eventLoopGroup,
            Aws::Crt::Iot::DeviceDefenderReportFormat::AWS_IDDRF_JSON,
            (uint64_t)1000000000UL,
            (uint64_t)1000000000UL,
            [](void *a) {
                auto data = reinterpret_cast<Aws::Crt::String *>(a);
                printf("######## DATA ##########\n%s", data->c_str());
            },
            (void *)&data);

        Aws::Crt::Iot::DeviceDefenderV1ReportTask task(allocator, taskConfig);
        task.StartTask();

        // {
        //     std::unique_lock<std::mutex> lock(mutex);
        //     cv.wait_until(lock, [&]() { return false; });
        // }

        std::this_thread::sleep_for(std::chrono::seconds(1));

        task.StopTask();

        std::this_thread::sleep_for(std::chrono::seconds(1));

        ASSERT_INT_EQUALS((int)Aws::Crt::Iot::DeviceDefenderV1ReportTaskStatus::Stopped, (int)task.GetStatus());

        mqttConnection->Disconnect();
        ASSERT_TRUE(mqttConnection);

        // NOLINTNEXTLINE
        ASSERT_FALSE(mqttClient);
    }

    Aws::Crt::TestCleanupAndWait();

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(DeviceDefenderResourceSafety, s_TestDeviceDefenderResourceSafety)
