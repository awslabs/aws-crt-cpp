/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>

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

        Aws::Crt::Iot::DeviceDefenderV1ReportTaskConfigBuilder taskBuilder(
            mqttConnection, eventLoopGroup, Aws::Crt::ByteCursorFromCString("TestThing"));
        taskBuilder.WithTaskPeriodNs((uint64_t)1000000000UL)
            .WithNetworkConnectionSamplePeriodNs((uint64_t)1000000000UL)
            .WithDefenderV1TaskCancelledHandler([](void *a) {
                auto data = reinterpret_cast<Aws::Crt::String *>(a);
                ASSERT_INT_EQUALS(0, data->compare("TestData"));
            })
            .WithDefenderV1TaskCancellationUserData(&data);

        Aws::Crt::Iot::DeviceDefenderV1ReportTaskConfig taskConfig = taskBuilder.Build();

        Aws::Crt::Iot::DeviceDefenderV1ReportTask task(allocator, taskConfig);
        ASSERT_INT_EQUALS((int)Aws::Crt::Iot::DeviceDefenderV1ReportTaskStatus::Ready, (int)task.GetStatus());

        task.StartTask();

        ASSERT_INT_EQUALS((int)Aws::Crt::Iot::DeviceDefenderV1ReportTaskStatus::Running, (int)task.GetStatus());
        std::this_thread::sleep_for(std::chrono::seconds(1));
        task.StopTask();
        std::this_thread::sleep_for(std::chrono::milliseconds(1100));

        mqttConnection->Disconnect();
        ASSERT_TRUE(mqttConnection);

        ASSERT_FALSE(mqttClient);

        ASSERT_INT_EQUALS((int)Aws::Crt::Iot::DeviceDefenderV1ReportTaskStatus::Stopped, (int)task.GetStatus());
    }

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(DeviceDefenderResourceSafety, s_TestDeviceDefenderResourceSafety)

static int s_TestDeviceDefenderFailedTest(Aws::Crt::Allocator *allocator, void *ctx)
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

        Aws::Crt::Iot::DeviceDefenderV1ReportTaskConfigBuilder taskBuilder(
            mqttConnection, eventLoopGroup, Aws::Crt::ByteCursorFromCString("TestThing"));
        taskBuilder.WithTaskPeriodNs((uint64_t)1000000000UL)
            .WithNetworkConnectionSamplePeriodNs((uint64_t)1000000000UL)
            .WithDeviceDefenderReportFormat(Aws::Crt::Iot::DeviceDefenderReportFormat::AWS_IDDRF_SHORT_JSON);

        Aws::Crt::Iot::DeviceDefenderV1ReportTaskConfig taskConfig = taskBuilder.Build();

        Aws::Crt::Iot::DeviceDefenderV1ReportTask task(allocator, taskConfig);

        task.OnDefenderV1TaskCancelled = [](void *a) {
            auto data = reinterpret_cast<Aws::Crt::String *>(a);
            ASSERT_INT_EQUALS(0, data->compare("TestData"));
        };
        task.cancellationUserdata = &data;

        ASSERT_INT_EQUALS((int)Aws::Crt::Iot::DeviceDefenderV1ReportTaskStatus::Ready, (int)task.GetStatus());

        task.StartTask();
        ASSERT_INT_EQUALS((int)Aws::Crt::Iot::DeviceDefenderV1ReportTaskStatus::Failed, (int)task.GetStatus());
        ASSERT_INT_EQUALS(AWS_ERROR_IOTDEVICE_DEFENDER_UNSUPPORTED_REPORT_FORMAT, task.LastError());

        mqttConnection->Disconnect();
        ASSERT_TRUE(mqttConnection);

        ASSERT_FALSE(mqttClient);
    }

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(DeviceDefenderFailedTest, s_TestDeviceDefenderFailedTest)