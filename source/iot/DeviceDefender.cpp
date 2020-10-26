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

                if (taskWrapper->OnDefenderV1TaskCancelled)
                {
                    taskWrapper->OnDefenderV1TaskCancelled(taskWrapper->cancellationUserdata);
                }
            }

            DeviceDefenderV1ReportTask::DeviceDefenderV1ReportTask(
                Aws::Crt::Allocator *allocator,
                std::shared_ptr<Mqtt::MqttConnection> mqttConnection,
                ByteCursor thingName,
                Io::EventLoopGroup &eventLoopGroup,
                DeviceDefenderReportFormat reportFormat,
                uint64_t taskPeriodNs,
                uint64_t networkConnectionSamplePeriodNs,
                OnDefenderV1TaskCancelledHandler &&onCancelled,
                void *cancellationUserdata) noexcept
                : OnDefenderV1TaskCancelled(std::move(onCancelled)), cancellationUserdata(cancellationUserdata),
                  m_allocator(allocator), m_status(DeviceDefenderV1ReportTaskStatus::Ready),
                  m_taskConfig{mqttConnection.get()->m_underlyingConnection,
                               thingName,
                               aws_event_loop_group_get_next_loop(eventLoopGroup.GetUnderlyingHandle()),
                               reportFormat,
                               taskPeriodNs,
                               networkConnectionSamplePeriodNs,
                               DeviceDefenderV1ReportTask::s_onDefenderV1TaskCancelled,
                               this},
                  m_lastError(0)
            {
            }

            DeviceDefenderV1ReportTask::DeviceDefenderV1ReportTask(DeviceDefenderV1ReportTask &&toMove) noexcept
                : OnDefenderV1TaskCancelled(std::move(toMove.OnDefenderV1TaskCancelled)),
                  cancellationUserdata(toMove.cancellationUserdata), m_allocator(toMove.m_allocator),
                  m_status(toMove.m_status), m_taskConfig(std::move(toMove.m_taskConfig)),
                  m_owningTask(toMove.m_owningTask), m_lastError(toMove.m_lastError)
            {
                toMove.OnDefenderV1TaskCancelled = nullptr;
                toMove.cancellationUserdata = nullptr;
                toMove.m_allocator = nullptr;
                toMove.m_status = DeviceDefenderV1ReportTaskStatus::Stopped;
                toMove.m_taskConfig = {0};
                toMove.m_owningTask = nullptr;
                toMove.m_lastError = AWS_ERROR_UNKNOWN;
            }

            DeviceDefenderV1ReportTask &DeviceDefenderV1ReportTask::operator=(
                DeviceDefenderV1ReportTask &&toMove) noexcept
            {
                OnDefenderV1TaskCancelled = std::move(toMove.OnDefenderV1TaskCancelled);
                cancellationUserdata = toMove.cancellationUserdata;
                m_allocator = toMove.m_allocator;
                m_status = toMove.m_status;
                m_taskConfig = std::move(toMove.m_taskConfig);
                m_owningTask = toMove.m_owningTask;
                m_lastError = toMove.m_lastError;

                toMove.OnDefenderV1TaskCancelled = nullptr;
                toMove.cancellationUserdata = nullptr;
                toMove.m_allocator = nullptr;
                toMove.m_status = DeviceDefenderV1ReportTaskStatus::Stopped;
                toMove.m_taskConfig = {0};
                toMove.m_owningTask = nullptr;
                toMove.m_lastError = AWS_ERROR_UNKNOWN;

                return *this;
            }

            DeviceDefenderV1ReportTaskStatus DeviceDefenderV1ReportTask::GetStatus() noexcept { return this->m_status; }

            void DeviceDefenderV1ReportTask::StartTask() noexcept
            {
                if (this->GetStatus() == DeviceDefenderV1ReportTaskStatus::Ready ||
                    this->GetStatus() == DeviceDefenderV1ReportTaskStatus::Stopped)
                {

                    this->m_owningTask = aws_iotdevice_defender_v1_report_task(this->m_allocator, &this->m_taskConfig);

                    if (this->m_owningTask == nullptr)
                    {
                        this->m_lastError = aws_last_error();
                        this->m_status = DeviceDefenderV1ReportTaskStatus::Failed;
                        if (this->OnDefenderV1TaskCancelled)
                        {
                            this->OnDefenderV1TaskCancelled(this->cancellationUserdata);
                        }

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
                    this->m_owningTask = nullptr;
                }
            }

            DeviceDefenderV1ReportTask::~DeviceDefenderV1ReportTask()
            {
                StopTask();
                this->m_owningTask = nullptr;
                this->m_allocator = nullptr;
                this->OnDefenderV1TaskCancelled = nullptr;
                this->cancellationUserdata = nullptr;
            }

            DeviceDefenderV1ReportTaskBuilder::DeviceDefenderV1ReportTaskBuilder(
                Aws::Crt::Allocator *allocator,
                std::shared_ptr<Mqtt::MqttConnection> mqttConnection,
                Io::EventLoopGroup &eventLoopGroup,
                ByteCursor thingName)
                : m_allocator(allocator), m_mqttConnection(mqttConnection), m_thingName(thingName),
                  m_eventLoopGroup(std::move(eventLoopGroup))
            {
                m_reportFormat = DeviceDefenderReportFormat::AWS_IDDRF_JSON;
                m_taskPeriodNs = 5UL * 60UL * 1000000000UL;
                m_networkConnectionSamplePeriodNs = 5UL * 60UL * 1000000000UL;
                m_onCancelled = nullptr;
                m_cancellationUserdata = nullptr;
            }

            DeviceDefenderV1ReportTaskBuilder &DeviceDefenderV1ReportTaskBuilder::WithDeviceDefenderReportFormat(
                DeviceDefenderReportFormat reportFormat) noexcept
            {
                m_reportFormat = reportFormat;
                return *this;
            }

            DeviceDefenderV1ReportTaskBuilder &DeviceDefenderV1ReportTaskBuilder::WithTaskPeriodNs(
                uint64_t taskPeriodNs) noexcept
            {
                m_taskPeriodNs = taskPeriodNs;
                return *this;
            }

            DeviceDefenderV1ReportTaskBuilder &DeviceDefenderV1ReportTaskBuilder::WithNetworkConnectionSamplePeriodNs(
                uint64_t networkConnectionSamplePeriodNs) noexcept
            {
                m_networkConnectionSamplePeriodNs = networkConnectionSamplePeriodNs;
                return *this;
            }

            DeviceDefenderV1ReportTaskBuilder &DeviceDefenderV1ReportTaskBuilder::WithDefenderV1TaskCancelledHandler(
                OnDefenderV1TaskCancelledHandler &&onCancelled) noexcept
            {
                m_onCancelled = std::move(onCancelled);
                return *this;
            }

            DeviceDefenderV1ReportTaskBuilder &DeviceDefenderV1ReportTaskBuilder::
                WithDefenderV1TaskCancellationUserData(void *cancellationUserdata) noexcept
            {
                m_cancellationUserdata = cancellationUserdata;
                return *this;
            }

            DeviceDefenderV1ReportTask DeviceDefenderV1ReportTaskBuilder::Build() noexcept
            {

                return DeviceDefenderV1ReportTask(
                    m_allocator,
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