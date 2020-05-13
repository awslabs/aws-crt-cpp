#include "aws/crt/io/EndPointMonitor.h"
#include <aws/common/clock.h>
#include <aws/common/task_scheduler.h>
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/io/event_loop.h>

using namespace Aws::Crt::Io;

EndPointMonitorOptions::EndPointMonitorOptions()
    : m_expectedPerSampleThroughput(0ULL), m_allowedFailureInterval(0ULL), m_schedulingLoop(nullptr),
      m_hostResolver(nullptr)
{
}

EndPointMonitor::SampleSum::SampleSum() : m_sampleSum(0ULL), m_numSamples(0ULL) {}

EndPointMonitor::SampleSum::SampleSum(uint64_t sample)
{
    m_sampleSum = sample & ((1ULL << 48ULL) - 1ULL);
    m_numSamples = sample >> 48ULL;
}

EndPointMonitor::SampleSum::SampleSum(uint64_t sampleSum, uint64_t numSamples)
    : m_sampleSum(sampleSum), m_numSamples(numSamples)
{
}

uint64_t EndPointMonitor::SampleSum::asUint64() const
{
    return (((uint64_t)m_numSamples) << 48ULL) | (uint64_t)m_sampleSum;
}

EndPointMonitor::EndPointMonitor(const String &address, const EndPointMonitorOptions &options)
    : m_address(address), m_options(options), m_processSamplesTask(nullptr), m_isInFailTable(false), m_sampleSum(0ULL),
      m_timeLastProcessed(0ULL), m_failureTime(0ULL)
{
    m_processSamplesTask = New<aws_task>(g_allocator);
    aws_task_init(m_processSamplesTask, EndPointMonitor::s_ProcessSamplesTask, this, "ProcessSamplesTask");

    uint64_t now = 0ULL;
    aws_sys_clock_get_ticks(&now); // TODO should be using event_loop_current_clock_time?
    m_timeLastProcessed = now;

    ScheduleNextProcessSamplesTask();
}

EndPointMonitor::~EndPointMonitor()
{
    if (m_processSamplesTask != nullptr)
    {
        aws_event_loop_cancel_task(m_options.m_schedulingLoop, m_processSamplesTask);
        m_processSamplesTask = nullptr;
    }
}

void EndPointMonitor::AddSample(uint64_t bytesPerSecond)
{
    SampleSum sampleSum(bytesPerSecond, 1ULL);
    m_sampleSum.fetch_add(sampleSum.asUint64());

    // TODO assert on overflow
}

void EndPointMonitor::SetIsInFailTable(bool status)
{
    m_isInFailTable.exchange(status);
}

bool EndPointMonitor::IsInFailTable() const
{
    return m_isInFailTable.load();
}

void EndPointMonitor::ScheduleNextProcessSamplesTask()
{
    uint64_t publishFrequencyNS = aws_timestamp_convert(1ULL, AWS_TIMESTAMP_SECS, AWS_TIMESTAMP_NANOS, NULL);

    uint64_t nowNS = 0;
    aws_event_loop_current_clock_time(m_options.m_schedulingLoop, &nowNS);

    aws_event_loop_schedule_task_future(m_options.m_schedulingLoop, m_processSamplesTask, nowNS + publishFrequencyNS);
}

void EndPointMonitor::s_ProcessSamplesTask(struct aws_task *task, void *arg, aws_task_status taskStatus)
{
    if (taskStatus == AWS_TASK_STATUS_CANCELED)
    {
        Delete<aws_task>(task, g_allocator);
        return;
    }

    EndPointMonitor *monitor = reinterpret_cast<EndPointMonitor *>(arg);
    monitor->ProcessSamples();
}

void EndPointMonitor::ProcessSamples()
{
    SampleSum sampleSum(m_sampleSum.exchange(0));

    uint64_t nowNS = 0ULL;
    aws_event_loop_current_clock_time(m_options.m_schedulingLoop, &nowNS);

    uint64_t timeElapsed = nowNS - m_timeLastProcessed;
    m_timeLastProcessed = nowNS;

    uint64_t expectedThroughputSum = sampleSum.m_numSamples * m_options.m_expectedPerSampleThroughput;

    if (sampleSum.m_sampleSum < expectedThroughputSum)
    {
        m_failureTime += timeElapsed;

        AWS_LOGF_INFO(
            AWS_LS_CRT_CPP_CANARY,
            "Endpoint Monitoring: Low througput detected for endpoint %s (%" PRIu64 " < %" PRIu64 ")",
            m_address.c_str(),
            (uint64_t)sampleSum.m_sampleSum,
            expectedThroughputSum);
    }
    else
    {
        m_failureTime = 0;
    }

    uint64_t allowedFailureIntervalNS =
        aws_timestamp_convert(m_options.m_allowedFailureInterval, AWS_TIMESTAMP_SECS, AWS_TIMESTAMP_NANOS, NULL);

    if (m_failureTime > allowedFailureIntervalNS)
    {
        AWS_LOGF_INFO(
            AWS_LS_CRT_CPP_CANARY,
            "Endpoint Monitoring: Recording failure for %s (%" PRIu64 " > %" PRIu64 ")",
            m_address.c_str(),
            m_failureTime,
            allowedFailureIntervalNS);

        aws_host_address hostAddress;
        AWS_ZERO_STRUCT(hostAddress);
        hostAddress.allocator = g_allocator;
        hostAddress.record_type = AWS_ADDRESS_RECORD_TYPE_A;
        hostAddress.host = aws_string_new_from_array(
            g_allocator, (uint8_t *)m_options.m_endPoint.c_str(), m_options.m_endPoint.length());
        hostAddress.address = aws_string_new_from_array(g_allocator, (uint8_t *)m_address.c_str(), m_address.length());

        aws_host_resolver_record_connection_failure(m_options.m_hostResolver, &hostAddress);

        aws_host_address_clean_up(&hostAddress);
    }

    ScheduleNextProcessSamplesTask();
}

EndPointMonitorManager::EndPointMonitorManager(const EndPointMonitorOptions &options) : m_options(options)
{
    AWS_FATAL_ASSERT(options.m_schedulingLoop != nullptr);
    AWS_FATAL_ASSERT(options.m_hostResolver != nullptr);

    aws_host_resolver_set_put_failure_table_callback(
        m_options.m_hostResolver, &EndPointMonitorManager::OnPutFailTable, this);
    aws_host_resolver_set_remove_failure_table_callback(
        m_options.m_hostResolver, &EndPointMonitorManager::OnRemoveFailTable, this);
}

EndPointMonitorManager::~EndPointMonitorManager()
{
    AWS_FATAL_ASSERT(m_options.m_hostResolver != nullptr);

    aws_host_resolver_set_put_failure_table_callback(m_options.m_hostResolver, nullptr, nullptr);
    aws_host_resolver_set_remove_failure_table_callback(m_options.m_hostResolver, nullptr, nullptr);
}

void EndPointMonitorManager::AttachMonitor(aws_http_connection *connection)
{
    std::lock_guard<std::mutex> lock(m_endPointMonitorsMutex);

    aws_host_address *hostAddress = aws_http_connection_get_host_address(connection);

    if (hostAddress == nullptr)
    {
        return;
    }

    EndPointMonitor *monitor = nullptr;

    Aws::Crt::String address(aws_string_c_str(hostAddress->address));
    auto endPointMonitorIt = m_endPointMonitors.find(address);

    if (endPointMonitorIt != m_endPointMonitors.end())
    {
        monitor = endPointMonitorIt->second.get();
        AWS_FATAL_ASSERT(monitor != nullptr);
    }
    else
    {
        AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "EndPointMonitorManager::AttachMonitor - Attaching monitor for address %s", address.c_str());
        monitor = new EndPointMonitor(address, m_options); // TODO use aws allocator with custom deleter
        m_endPointMonitors.emplace(address, std::unique_ptr<EndPointMonitor>(monitor));
    }

    aws_http_connection_set_endpoint_monitor(connection, (void *)monitor);
}

void EndPointMonitorManager::OnPutFailTable(aws_host_address *host_address, void *user_data)
{
    EndPointMonitorManager *endPointMonitorManager = (EndPointMonitorManager *)user_data;
    std::lock_guard<std::mutex> lock(endPointMonitorManager->m_endPointMonitorsMutex);

    String address(aws_string_c_str(host_address->address));
    auto endPointMonitorIt = endPointMonitorManager->m_endPointMonitors.find(address);

    if (endPointMonitorIt == endPointMonitorManager->m_endPointMonitors.end())
    {
        AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "EndPointMonitorManager::OnPutFailTable - Could not find monitor for address %s", address.c_str());
        return;
    }

    EndPointMonitor *monitor = endPointMonitorIt->second.get();

    monitor->SetIsInFailTable(true);
}

void EndPointMonitorManager::OnRemoveFailTable(aws_host_address *host_address, void *user_data)
{
    EndPointMonitorManager *endPointMonitorManager = (EndPointMonitorManager *)user_data;
    std::lock_guard<std::mutex> lock(endPointMonitorManager->m_endPointMonitorsMutex);

    String address(aws_string_c_str(host_address->address));
    auto endPointMonitorIt = endPointMonitorManager->m_endPointMonitors.find(address);

    if (endPointMonitorIt == endPointMonitorManager->m_endPointMonitors.end())
    {
        AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "EndPointMonitorManager::OnRemoveFailTable - Could not find monitor for address %s", address.c_str());
        return;
    }

    EndPointMonitor *monitor = endPointMonitorIt->second.get();

    monitor->SetIsInFailTable(false);
}
