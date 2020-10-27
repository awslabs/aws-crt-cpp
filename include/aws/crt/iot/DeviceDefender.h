#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/mqtt/MqttClient.h>

#include <aws/iotdevice/device_defender.h>

namespace Aws
{
    namespace Crt
    {

        namespace Io
        {
            class EventLoopGroup;
        }

        namespace Mqtt
        {
            class MqttConnection;
        }

        namespace Iot
        {

            class DeviceDefenderV1ReportTask;
            class DeviceDefenderV1ReportTaskBuilder;

            /**
             * Invoked upon DeviceDefender V1 task cancellation.
             */
            using OnDefenderV1TaskCancelledHandler = std::function<void(void *)>;

            using DeviceDefenderReportFormat = aws_iotdevice_defender_report_format;

            /**
             * Enum used to expose the status of a DeviceDefenderV1 task.
             */
            enum class DeviceDefenderV1ReportTaskStatus
            {
                Ready = 0,
                Running = 1,
                Stopped = 2,
            };

            /**
             * Represents a persistent DeviceDefender V1 task.
             */
            class AWS_CRT_CPP_API DeviceDefenderV1ReportTask final
            {
                friend DeviceDefenderV1ReportTaskBuilder;

              public:
                ~DeviceDefenderV1ReportTask();
                DeviceDefenderV1ReportTask(const DeviceDefenderV1ReportTask &) = delete;
                DeviceDefenderV1ReportTask(DeviceDefenderV1ReportTask &&) noexcept;
                DeviceDefenderV1ReportTask &operator=(const DeviceDefenderV1ReportTask &) = delete;
                DeviceDefenderV1ReportTask &operator=(DeviceDefenderV1ReportTask &&) noexcept;

                /**
                 * Initiates stopping of the Defender V1 task.
                 */
                void StopTask() noexcept;

                /**
                 * Initiates Defender V1 reporting task.
                 */
                int StartTask() noexcept;

                /**
                 * Returns the task status.
                 */
                DeviceDefenderV1ReportTaskStatus GetStatus() noexcept;

                OnDefenderV1TaskCancelledHandler OnDefenderV1TaskCancelled;

                void *cancellationUserdata;

                /**
                 * @return the value of the last aws error encountered by operations on this instance.
                 */
                int LastError() const noexcept { return m_lastError; }

              private:
                Crt::Allocator *m_allocator;
                DeviceDefenderV1ReportTaskStatus m_status;
                aws_iotdevice_defender_report_task_config m_taskConfig;
                aws_iotdevice_defender_v1_task *m_owningTask;
                int m_lastError;

                DeviceDefenderV1ReportTask(
                    Crt::Allocator *allocator,
                    std::shared_ptr<Mqtt::MqttConnection> mqttConnection,
                    const Crt::String &thingName,
                    Io::EventLoopGroup &eventLoopGroup,
                    DeviceDefenderReportFormat reportFormat,
                    uint64_t taskPeriodNs,
                    uint64_t networkConnectionSamplePeriodNs,
                    OnDefenderV1TaskCancelledHandler &&onCancelled = NULL,
                    void *cancellationUserdata = nullptr) noexcept;

                static void s_onDefenderV1TaskCancelled(void *userData);
            };

            /**
             * Represents a builder for creating a DeviceDefenderV1ReportTask object.
             */
            class AWS_CRT_CPP_API DeviceDefenderV1ReportTaskBuilder final
            {
              public:
                DeviceDefenderV1ReportTaskBuilder(
                    Crt::Allocator *allocator,
                    std::shared_ptr<Mqtt::MqttConnection> mqttConnection,
                    Io::EventLoopGroup &eventLoopGroup,
                    const Crt::String &thingName);

                /**
                 * Sets the device defender report format, or defaults to AWS_IDDRF_JSON.
                 */
                DeviceDefenderV1ReportTaskBuilder &WithDeviceDefenderReportFormat(
                    DeviceDefenderReportFormat reportFormat) noexcept;

                /**
                 * Sets the task period nanoseconds. Defaults to 5 minutes.
                 */
                DeviceDefenderV1ReportTaskBuilder &WithTaskPeriodNs(uint64_t taskPeriodNs) noexcept;

                /**
                 * Sets the network connection sample period nanoseconds. Defaults to 5 minutes.
                 */
                DeviceDefenderV1ReportTaskBuilder &WithNetworkConnectionSamplePeriodNs(
                    uint64_t networkConnectionSamplePeriodNs) noexcept;

                /**
                 * Sets the task cancelled handler function.
                 */
                DeviceDefenderV1ReportTaskBuilder &WithDefenderV1TaskCancelledHandler(
                    OnDefenderV1TaskCancelledHandler &&onCancelled) noexcept;

                /**
                 * Sets the user data for the task cancelled handler function.
                 */
                DeviceDefenderV1ReportTaskBuilder &WithDefenderV1TaskCancellationUserData(
                    void *cancellationUserdata) noexcept;

                /**
                 * Builds a device defender v1 task object from the set options.
                 */
                DeviceDefenderV1ReportTask Build() noexcept;

              private:
                Crt::Allocator *m_allocator;
                std::shared_ptr<Mqtt::MqttConnection> m_mqttConnection;
                Crt::String m_thingName;
                Io::EventLoopGroup m_eventLoopGroup;
                DeviceDefenderReportFormat m_reportFormat;
                uint64_t m_taskPeriodNs;
                uint64_t m_networkConnectionSamplePeriodNs;
                OnDefenderV1TaskCancelledHandler m_onCancelled;
                void *m_cancellationUserdata;
            };

        } // namespace Iot
    }     // namespace Crt
} // namespace Aws