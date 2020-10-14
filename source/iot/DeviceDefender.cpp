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
                auto *taskWrapper = reinterpret_cast<DeviceDefenderV1ReportTask *>(userData);
                taskWrapper->m_status = DeviceDefenderV1ReportTaskStatus::Stopped;

                if (taskWrapper->m_config.onCancelled)
                {
                    taskWrapper->m_config.onCancelled(taskWrapper->m_config.cancellationUserdata);
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
                if (this->GetStatus() == DeviceDefenderV1ReportTaskStatus::Ready)
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
                    if (this->m_owningTask == nullptr)
                    {
                        this->m_lastError = aws_last_error();
                        this->m_status = DeviceDefenderV1ReportTaskStatus::Failed;
                        return;
                    }
                    this->m_status = DeviceDefenderV1ReportTaskStatus::Running;
                }
            }

            void DeviceDefenderV1ReportTask::StopTask() noexcept
            {
                if (this->GetStatus() == DeviceDefenderV1ReportTaskStatus::Running)
                {
                    aws_iotdevice_defender_v1_stop_task(this->m_owningTask);
                }
            }

            DeviceDefenderV1ReportTask::~DeviceDefenderV1ReportTask()
            {
                StopTask();
                this->m_owningTask = nullptr;
            }

            DeviceDefenderV1ReportTaskConfigBuilder::DeviceDefenderV1ReportTaskConfigBuilder(
                std::shared_ptr<Mqtt::MqttConnection> mqttConnection,
                Io::EventLoopGroup &eventLoopGroup,
                ByteCursor thingName)
                : m_mqttConnection(mqttConnection), m_thingName(thingName), m_eventLoopGroup(std::move(eventLoopGroup))
            {
                m_reportFormat = DeviceDefenderReportFormat::AWS_IDDRF_JSON;
                m_taskPeriodNs = 5UL * 60UL * 1000000000UL;
                m_networkConnectionSamplePeriodNs = 5UL * 60UL * 1000000000UL;
                m_onCancelled = nullptr;
                m_cancellationUserdata = nullptr;
            }

            DeviceDefenderV1ReportTaskConfigBuilder &DeviceDefenderV1ReportTaskConfigBuilder::
                WithDeviceDefenderReportFormat(DeviceDefenderReportFormat reportFormat) noexcept
            {
                m_reportFormat = reportFormat;
                return *this;
            }

            DeviceDefenderV1ReportTaskConfigBuilder &DeviceDefenderV1ReportTaskConfigBuilder::WithTaskPeriodNs(
                uint64_t taskPeriodNs) noexcept
            {
                m_taskPeriodNs = taskPeriodNs;
                return *this;
            }

            DeviceDefenderV1ReportTaskConfigBuilder &DeviceDefenderV1ReportTaskConfigBuilder::
                WithNetworkConnectionSamplePeriodNs(uint64_t networkConnectionSamplePeriodNs) noexcept
            {
                m_networkConnectionSamplePeriodNs = networkConnectionSamplePeriodNs;
                return *this;
            }

            DeviceDefenderV1ReportTaskConfigBuilder &DeviceDefenderV1ReportTaskConfigBuilder::
                WithDefenderV1TaskCancelledHandler(OnDefenderV1TaskCancelledHandler &&onCancelled) noexcept
            {
                m_onCancelled = std::move(onCancelled);
                return *this;
            }

            DeviceDefenderV1ReportTaskConfigBuilder &DeviceDefenderV1ReportTaskConfigBuilder::
                WithDefenderV1TaskCancellationUserData(void *cancellationUserdata) noexcept
            {
                m_cancellationUserdata = cancellationUserdata;
                return *this;
            }

            DeviceDefenderV1ReportTaskConfig DeviceDefenderV1ReportTaskConfigBuilder::Build() noexcept
            {

                return DeviceDefenderV1ReportTaskConfig(
                    m_mqttConnection,
                    m_thingName,
                    m_eventLoopGroup,
                    m_reportFormat,
                    m_taskPeriodNs,
                    m_networkConnectionSamplePeriodNs,
                    static_cast<OnDefenderV1TaskCancelledHandler &&>(m_onCancelled),
                    m_cancellationUserdata);
            }

        } // namespace Iot
    }     // namespace Crt
} // namespace Aws