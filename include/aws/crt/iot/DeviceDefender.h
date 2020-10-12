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
             * Represents a unique configuration for a device defender V1 report task. You can use a single instance of
             * this class PER task you want to create.
             */
            class AWS_CRT_CPP_API DeviceDefenderV1ReportTaskConfig final
            {

                friend class DeviceDefenderV1ReportTask;
                // friend class DeviceDefenderV1ReportTaskConfigBuilder;

              public:
                /**
                 * Creates a client configuration for use with making new AWS Iot specific MQTT Connections with MTLS.
                 */
                DeviceDefenderV1ReportTaskConfig(
                    std::shared_ptr<Mqtt::MqttConnection> mqttConnection,
                    ByteCursor thingName,
                    Io::EventLoopGroup &eventLoopGroup,
                    DeviceDefenderReportFormat reportFormat,
                    uint64_t taskPeriodNs,
                    uint64_t networkConnectionSamplePeriodNs,
                    OnDefenderV1TaskCancelledHandler &&onCancelled,
                    void *cancellationUserdata) noexcept;

                /**
                 * @return the value of the last aws error encountered by operations on this instance.
                 */
                int LastError() const noexcept { return m_lastError; }

              private:
                DeviceDefenderV1ReportTaskConfig(int lastError) noexcept;
                std::shared_ptr<Mqtt::MqttConnection> mqttConnection;
                ByteCursor thingName;
                Io::EventLoopGroup &eventLoopGroup;
                DeviceDefenderReportFormat reportFormat;
                uint64_t taskPeriodNs;
                uint64_t networkConnectionSamplePeriodNs;
                OnDefenderV1TaskCancelledHandler onCancelled;
                void *cancellationUserdata;
                int m_lastError;

                static DeviceDefenderV1ReportTaskConfig CreateInvalid(int lastError) noexcept;
            };

            /**
             * Represents configuration parameters for building a DeviceDefenderV1ReportTaskConfig object. You can use a
             * single instance of this class PER DeviceDefenderV1ReportTaskConfig you want to generate. If you want to
             * generate a config for a different Thing or connection etc... you need a new instance of this class.
             */
            // class AWS_CRT_CPP_API DeviceDefenderV1ReportTaskConfigBuilder final
            // {
            //   public:
            //     DeviceDefenderV1ReportTaskConfigBuilder();

            //     /**
            //      * Sets mqtt connection to use.
            //      */
            //     DeviceDefenderV1ReportTaskConfigBuilder &WithMqttConnection(
            //         std::shared_ptr<Mqtt::MqttConnection> mqttConnection);

            //     /**
            //      * Sets thing name to use.
            //      */
            //     DeviceDefenderV1ReportTaskConfigBuilder &WithThingName(ByteCursor thingName);

            //     /**
            //      * Sets event loop group to use.
            //      */
            //     DeviceDefenderV1ReportTaskConfigBuilder &WithEventLoopGroup(Io::EventLoopGroup *eventLoopGroup);

            //     /**
            //      * Sets the device defender report format, or defaults to AWS_IDDRF_JSON.
            //      */
            //     DeviceDefenderV1ReportTaskConfigBuilder &WithDeviceDefenderReportFormat(
            //         DeviceDefenderReportFormat reportFormat) noexcept;

            //     /**
            //      * Sets the task period nanoseconds. Defaults to 5 minutes.
            //      */
            //     DeviceDefenderV1ReportTaskConfigBuilder &WithTaskPeriodNs(uint64_t taskPeriodNs) noexcept;

            //     /**
            //      * Sets the network connection sample period nanoseconds. Defaults to 5 minutes.
            //      */
            //     DeviceDefenderV1ReportTaskConfigBuilder &WithNetworkConnectionSamplePeriodNs(
            //         uint64_t networkConnectionSamplePeriodNs) noexcept;

            //     /**
            //      * Sets the task cancelled handler function.
            //      */
            //     DeviceDefenderV1ReportTaskConfigBuilder &WithDefenderV1TaskCancelledHandler(
            //         OnDefenderV1TaskCancelledHandler &&onCancelled) noexcept;

            //     /**
            //      * Sets the user data for the task cancelled handler function.
            //      */
            //     DeviceDefenderV1ReportTaskConfigBuilder &WithDefenderV1TaskCancelationUserData(
            //         void *cancellationUserdata) noexcept;

            //     /**
            //      * Builds a device defender v1 task configuration object from the set options.
            //      */
            //     DeviceDefenderV1ReportTaskConfig Build() noexcept;
            //     /**
            //      * @return true if the instance is in a valid state, false otherwise.
            //      */
            //     explicit operator bool() const noexcept { return m_isGood; }
            //     /**
            //      * @return the value of the last aws error encountered by operations on this instance.
            //      */
            //     int LastError() const noexcept { return aws_last_error(); }

            //   private:
            //     std::shared_ptr<Mqtt::MqttConnection> m_mqttConnection;
            //     ByteCursor m_thingName;
            //     Io::EventLoopGroup *m_eventLoopGroup;
            //     DeviceDefenderReportFormat m_reportFormat;
            //     uint64_t m_taskPeriodNs;
            //     uint64_t m_networkConnectionSamplePeriodNs;
            //     OnDefenderV1TaskCancelledHandler m_onCancelled;
            //     void *m_cancellationUserdata;
            //     bool m_isGood;
            // };

            /**
             * Represents a persistent DeviceDefender V1 task.
             */
            class AWS_CRT_CPP_API DeviceDefenderV1ReportTask final
            {
                friend class DeviceDefender;

              public:
                DeviceDefenderV1ReportTask(
                    Aws::Crt::Allocator *allocator,
                    const DeviceDefenderV1ReportTaskConfig &config) noexcept;
                ~DeviceDefenderV1ReportTask();
                DeviceDefenderV1ReportTask(const DeviceDefenderV1ReportTask &) = delete;
                DeviceDefenderV1ReportTask(DeviceDefenderV1ReportTask &&) = delete;
                DeviceDefenderV1ReportTask &operator=(const DeviceDefenderV1ReportTask &) = delete;
                DeviceDefenderV1ReportTask &operator=(DeviceDefenderV1ReportTask &&) = delete;

                /**
                 * Initiates stopping of the Defender V1 task.
                 */
                void StopTask() noexcept;

                /**
                 * Initiates Defender V1 reporting task.
                 */
                void StartTask();

                /**
                 * Returns the task status.
                 */
                DeviceDefenderV1ReportTaskStatus GetStatus() noexcept;

                OnDefenderV1TaskCancelledHandler OnDefenderV1TaskCancelled;

                /**
                 * @return the value of the last aws error encountered by operations on this instance.
                 */
                int LastError() const noexcept { return m_lastError; }

              private:
                Aws::Crt::Allocator *m_allocator;
                DeviceDefenderV1ReportTaskConfig m_config;
                aws_iotdevice_defender_v1_task *m_owningTask;
                DeviceDefenderV1ReportTaskStatus m_status;
                int m_lastError;

                static void s_onDefenderV1TaskCancelled(void *userData);
            };

        } // namespace Iot
    }     // namespace Crt
} // namespace Aws