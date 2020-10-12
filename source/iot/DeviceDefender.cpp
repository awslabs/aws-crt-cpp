/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/iot/DeviceDefender.h>

namespace Aws
{
    namespace Crt
    {

        namespace Iot
        {

            void DeviceDefenderV1ReportTask::s_onDefenderV1TaskCancelled(void *userData)
            {
                auto taskWrapper = reinterpret_cast<DeviceDefenderV1ReportTask *>(userData);
                taskWrapper->m_status = DeviceDefenderV1ReportTaskStatus::Stopped;

                if (taskWrapper->OnDefenderV1TaskCancelled)
                {
                    taskWrapper->OnDefenderV1TaskCancelled(taskWrapper->m_config.cancellationUserdata);
                }
            }

            DeviceDefenderV1ReportTaskConfig::DeviceDefenderV1ReportTaskConfig(
                std::shared_ptr<Mqtt::MqttConnection> mqttConnection,
                ByteCursor thingName,
                Io::EventLoopGroup &eventLoopGroup,
                DeviceDefenderReportFormat reportFormat,
                uint64_t taskPeriodNs,
                uint64_t networkConnectionSamplePeriodNs,
                OnDefenderV1TaskCancelledHandler &&onCancelled,
                void *cancellationUserdata) noexcept
                : mqttConnection(mqttConnection), thingName(thingName), eventLoopGroup(eventLoopGroup),
                  reportFormat(reportFormat), taskPeriodNs(taskPeriodNs),
                  networkConnectionSamplePeriodNs(networkConnectionSamplePeriodNs), onCancelled(std::move(onCancelled)),
                  cancellationUserdata(cancellationUserdata)
            {
            }

            DeviceDefenderV1ReportTask::DeviceDefenderV1ReportTask(
                Aws::Crt::Allocator *allocator,
                const DeviceDefenderV1ReportTaskConfig &config) noexcept
                : m_allocator(allocator), m_config(config), m_status(DeviceDefenderV1ReportTaskStatus::Ready)
            {
            }

            DeviceDefenderV1ReportTaskStatus DeviceDefenderV1ReportTask::GetStatus() noexcept { return this->m_status; }

            void DeviceDefenderV1ReportTask::StartTask()
            {

                struct aws_iotdevice_defender_report_task_config config = {
                    this->m_config.mqttConnection.get()->m_underlyingConnection,
                    this->m_config.thingName,
                    aws_event_loop_group_get_next_loop(this->m_config.eventLoopGroup.GetUnderlyingHandle()),
                    this->m_config.reportFormat,
                    this->m_config.taskPeriodNs,
                    this->m_config.networkConnectionSamplePeriodNs,
                    DeviceDefenderV1ReportTask::s_onDefenderV1TaskCancelled,
                    this,
                };

                this->m_owningTask = aws_iotdevice_defender_v1_report_task(this->m_allocator, &config);
            } // namespace Iot

            void DeviceDefenderV1ReportTask::StopTask() noexcept
            {
                aws_iotdevice_defender_v1_stop_task(this->m_owningTask);
            }

            DeviceDefenderV1ReportTask::~DeviceDefenderV1ReportTask()
            {
                if (this->GetStatus() != DeviceDefenderV1ReportTaskStatus::Stopped)
                {
                    this->StopTask();
                }
            }

            // DeviceDefenderV1ReportTaskConfig DeviceDefenderV1ReportTaskConfig::CreateInvalid(int lastError) noexcept
            // {
            //     return DeviceDefenderV1ReportTaskConfig(lastError);
            // }

            // DeviceDefenderV1ReportTaskConfigBuilder::DeviceDefenderV1ReportTaskConfigBuilder() : m_isGood(false) {}

            // DeviceDefenderV1ReportTaskConfigBuilder &DeviceDefenderV1ReportTaskConfigBuilder::WithThingName(
            //     ByteCursor thingName)
            // {
            //     m_thingName = thingName;
            // }

            // DeviceDefenderV1ReportTaskConfigBuilder &DeviceDefenderV1ReportTaskConfigBuilder::WithEventLoopGroup(
            //     Io::EventLoopGroup *eventLoopGroup)
            // {
            //     m_eventLoopGroup = eventLoopGroup;
            // }

            // DeviceDefenderV1ReportTaskConfigBuilder &DeviceDefenderV1ReportTaskConfigBuilder::
            //     WithDeviceDefenderReportFormat(DeviceDefenderReportFormat reportFormat)
            // {
            //     m_reportFormat = reportFormat;
            // }

            // DeviceDefenderV1ReportTaskConfigBuilder &DeviceDefenderV1ReportTaskConfigBuilder::WithTaskPeriodNs(
            //     uint64_t taskPeriodNs)
            // {
            //     m_taskPeriodNs = taskPeriodNs;
            // }

            // DeviceDefenderV1ReportTaskConfigBuilder &DeviceDefenderV1ReportTaskConfigBuilder::
            //     WithNetworkConnectionSamplePeriodNs(uint64_t networkConnectionSamplePeriodNs)
            // {
            //     m_networkConnectionSamplePeriodNs = networkConnectionSamplePeriodNs;
            // }

            // DeviceDefenderV1ReportTaskConfigBuilder &DeviceDefenderV1ReportTaskConfigBuilder::
            //     WithDefenderV1TaskCancelledHandler(OnDefenderV1TaskCancelledHandler &&onCancelled)
            // {
            //     m_onCancelled = std::move(onCancelled);
            // }

            // DeviceDefenderV1ReportTaskConfigBuilder &DeviceDefenderV1ReportTaskConfigBuilder::
            //     WithDefenderV1TaskCancelationUserData(void *cancellationUserdata)
            // {
            //     m_cancellationUserdata = cancellationUserdata;
            // }

            // DeviceDefenderV1ReportTaskConfig DeviceDefenderV1ReportTaskConfigBuilder::Build() noexcept
            // {
            //     if (!m_isGood)
            //     {
            //         return DeviceDefenderV1ReportTaskConfig::CreateInvalid(aws_last_error());
            //     }

            //     if (!m_taskPeriodNs)
            //     {
            //         m_taskPeriodNs = 5UL * 60UL * 1000000000UL;
            //     }

            //     if (!m_networkConnectionSamplePeriodNs)
            //     {
            //         m_networkConnectionSamplePeriodNs = 5UL * 60UL * 1000000000UL;
            //     }

            //     return DeviceDefenderV1ReportTaskConfig(
            //         m_mqttConnection,
            //         m_thingName,
            //         (Io::EventLoopGroup)*m_eventLoopGroup,
            //         m_reportFormat,
            //         m_taskPeriodNs,
            //         m_networkConnectionSamplePeriodNs,
            //         m_onCancelled,
            //         m_cancellationUserdata);
            // }

        } // namespace Iot
    }     // namespace Crt
} // namespace Aws