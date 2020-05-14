#include "aws/crt/io/EndPointMonitor.h"
#include "aws/crt/Api.h"
#include "aws/crt/DateTime.h"
#include "aws/crt/Types.h"
#include "aws/crt/http/HttpConnection.h"
#include <aws/common/clock.h>
#include <aws/common/task_scheduler.h>
#include <aws/io/event_loop.h>
#include <iomanip>
#include <iostream>

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

    aws_event_loop_current_clock_time(m_options.m_schedulingLoop, &m_timeLastProcessed);

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

    uint64_t prevTimeLastProcessed = m_timeLastProcessed;
    m_timeLastProcessed = nowNS;

    if (sampleSum.m_numSamples > 0 &&
        (sampleSum.m_sampleSum / sampleSum.m_numSamples) < m_options.m_expectedPerSampleThroughput)
    {
        m_failureTime += timeElapsed;

        AWS_LOGF_INFO(
            AWS_LS_CRT_CPP_CANARY,
            "Endpoint Monitoring: Low throughput detected for endpoint %s (%" PRIu64 " < %" PRIu64 ")",
            m_address.c_str(),
            (uint64_t)sampleSum.m_sampleSum,
            m_options.m_expectedPerSampleThroughput);
    }
    else
    {
        m_failureTime = 0ULL;
    }

    uint64_t allowedFailureIntervalNS =
        aws_timestamp_convert(m_options.m_allowedFailureInterval, AWS_TIMESTAMP_SECS, AWS_TIMESTAMP_NANOS, NULL);

    bool reportedConnectionFailure = false;

    if (m_failureTime > allowedFailureIntervalNS)
    {
        if (!m_isInFailTable.load())
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
            hostAddress.address =
                aws_string_new_from_array(g_allocator, (uint8_t *)m_address.c_str(), m_address.length());

            aws_host_resolver_record_connection_failure(m_options.m_hostResolver, &hostAddress);

            aws_host_address_clean_up(&hostAddress);

            reportedConnectionFailure = true;
        }

        m_failureTime = 0ULL;
    }

    if (sampleSum.m_numSamples > 0ULL)
    {
        m_history.m_entries.emplace_back(
            prevTimeLastProcessed,
            (uint64_t)sampleSum.m_sampleSum,
            (uint32_t)sampleSum.m_numSamples,
            reportedConnectionFailure);
    }

    ScheduleNextProcessSamplesTask();
}

EndPointMonitorManager::EndPointMonitorManager(const EndPointMonitorOptions &options) : m_options(options)
{
    AWS_FATAL_ASSERT(options.m_schedulingLoop != nullptr);
    AWS_FATAL_ASSERT(options.m_hostResolver != nullptr);
}

EndPointMonitorManager::~EndPointMonitorManager()
{
    AWS_FATAL_ASSERT(m_options.m_hostResolver != nullptr);

    // TODO should check to make sure the callback is still owned by this object
    aws_host_resolver_set_put_failure_table_callback(m_options.m_hostResolver, nullptr, nullptr);
    aws_host_resolver_set_remove_failure_table_callback(m_options.m_hostResolver, nullptr, nullptr);
}

void EndPointMonitorManager::SetupCallbacks()
{
    AWS_FATAL_ASSERT(m_options.m_hostResolver != nullptr);

    aws_host_resolver_set_put_failure_table_callback(
        m_options.m_hostResolver, &EndPointMonitorManager::OnPutFailTable, this);
    aws_host_resolver_set_remove_failure_table_callback(
        m_options.m_hostResolver, &EndPointMonitorManager::OnRemoveFailTable, this);
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

        AWS_LOGF_TRACE(
            AWS_LS_CRT_CPP_CANARY,
            "[%" PRIx64 "] EndPointMonitorManager::AttachMonitor - Attaching existing monitor for address %s",
            (uint64_t)this,
            address.c_str());
    }
    else
    {
        AWS_LOGF_TRACE(
            AWS_LS_CRT_CPP_CANARY,
            "[%" PRIx64 "] EndPointMonitorManager::AttachMonitor - Attaching new monitor for address %s",
            (uint64_t)this,
            address.c_str());

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

    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY,
        "EndPointMonitorManager::OnPutFailTable - Address %s placed in fail table",
        address.c_str());

    if (endPointMonitorIt == endPointMonitorManager->m_endPointMonitors.end())
    {
        AWS_LOGF_ERROR(
            AWS_LS_CRT_CPP_CANARY,
            "[%" PRIx64
            "] EndPointMonitorManager::OnPutFailTable - Could not find monitor for address %s, with %d monitors.",
            (uint64_t)endPointMonitorManager,
            address.c_str(),
            (uint32_t)endPointMonitorManager->m_endPointMonitors.size());

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

    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY,
        "EndPointMonitorManager::OnRemoveFailTable - Address %s removed from fail table",
        address.c_str());

    if (endPointMonitorIt == endPointMonitorManager->m_endPointMonitors.end())
    {
        AWS_LOGF_ERROR(
            AWS_LS_CRT_CPP_CANARY,
            "EndPointMonitorManager::OnRemoveFailTable - Could not find monitor for address %s, with %d monitors.",
            address.c_str(),
            (uint32_t)endPointMonitorManager->m_endPointMonitors.size());
        return;
    }

    EndPointMonitor *monitor = endPointMonitorIt->second.get();

    monitor->SetIsInFailTable(false);
}

std::shared_ptr<Aws::Crt::StringStream> EndPointMonitorManager::GenerateEndPointCSV()
{
    std::lock_guard<std::mutex> lock(m_endPointMonitorsMutex);

    uint64_t minTime = ~0ULL;
    uint64_t maxTime = 0ULL;

    std::shared_ptr<StringStream> endPointCSVContents = MakeShared<StringStream>(g_allocator);

    for (auto it = m_endPointMonitors.begin(); it != m_endPointMonitors.end(); ++it)
    {
        EndPointMonitor *monitor = it->second.get();
        const EndPointMonitor::History &history = monitor->GetHistory();

        for (const EndPointMonitor::HistoryEntry &historyEntry : history.m_entries)
        {
            minTime = std::min(minTime, historyEntry.m_timeStamp);
            maxTime = std::max(maxTime, historyEntry.m_timeStamp);
        }
    }

    if (maxTime < minTime)
    {
        return endPointCSVContents;
    }

    uint64_t minTimeSec = aws_timestamp_convert(minTime, AWS_TIMESTAMP_NANOS, AWS_TIMESTAMP_SECS, NULL);
    uint64_t maxTimeSec = aws_timestamp_convert(maxTime, AWS_TIMESTAMP_NANOS, AWS_TIMESTAMP_SECS, NULL);
    uint64_t timeInterval = maxTimeSec - minTimeSec;

    Vector<uint64_t> totalSampleCount;
    Vector<uint64_t> totalSample;
    Vector<EndPointMonitor::HistoryEntry> rowHistory;

    *endPointCSVContents << "Endpoint";

    for (uint64_t i = 0; i <= timeInterval; ++i)
    {
        rowHistory.emplace_back();
        totalSampleCount.push_back(0ULL);
        totalSample.push_back(0ULL);

        DateTime dateTime((uint64_t)(i * 1000ULL));

        StringStream dateTimeString;
        dateTimeString << std::setfill('0') << std::setw(2) << (uint32_t)dateTime.GetHour() << ":" << std::setw(2)
                       << (uint32_t)dateTime.GetMinute() << ":" << std::setw(2) << (uint32_t)dateTime.GetSecond();

        *endPointCSVContents << "," << dateTimeString.str().c_str();
    }

    *endPointCSVContents << std::endl;

    for (auto it = m_endPointMonitors.begin(); it != m_endPointMonitors.end(); ++it)
    {
        EndPointMonitor *monitor = it->second.get();
        const EndPointMonitor::History &history = monitor->GetHistory();

        for (const Io::EndPointMonitor::HistoryEntry &historyEntry : history.m_entries)
        {
            uint64_t timeStampSec =
                aws_timestamp_convert(historyEntry.m_timeStamp, AWS_TIMESTAMP_NANOS, AWS_TIMESTAMP_SECS, NULL);
            uint64_t relativeSec = timeStampSec - minTimeSec;

            rowHistory[relativeSec] = historyEntry;
            totalSampleCount[relativeSec] += historyEntry.m_numSamples;
            totalSample[relativeSec] += historyEntry.m_bytesPerSecond;
        }

        *endPointCSVContents << monitor->GetAddress().c_str();

        for (auto rowHistoryIt = rowHistory.begin(); rowHistoryIt != rowHistory.end(); ++rowHistoryIt)
        {
            double GbPerSecondAvg = 0.0;

            if (rowHistoryIt->m_numSamples > 0)
            {
                GbPerSecondAvg = ((double)rowHistoryIt->m_bytesPerSecond / (double)rowHistoryIt->m_numSamples) * 8.0 /
                                 1000.0 / 1000.0 / 1000.0;
            }

            *endPointCSVContents << "," << GbPerSecondAvg;

            if (rowHistoryIt->m_putInFailTable)
            {
                *endPointCSVContents << "*";
            }

            *rowHistoryIt = EndPointMonitor::HistoryEntry();
        }

        *endPointCSVContents << std::endl;
    }

    *endPointCSVContents << "Overall Average";

    for (size_t i = 0; i < totalSample.size(); ++i)
    {
        double GbPerSecondAvg = 0.0;

        if (totalSampleCount[i] > 0ULL)
        {
            GbPerSecondAvg = ((double)totalSample[i] / (double)totalSampleCount[i]) * 8.0 / 1000.0 / 1000.0 / 1000.0;
        }

        *endPointCSVContents << "," << GbPerSecondAvg;
    }

    *endPointCSVContents << std::endl;

    size_t numCols = totalSample.size() + 1;

    for (size_t i = 0; i < numCols - 1; ++i)
    {
        *endPointCSVContents << ",";
    }

    *endPointCSVContents << std::endl;

    *endPointCSVContents << "Expected Avg Per Sample,"
                         << (m_options.m_expectedPerSampleThroughput * 8.0 / 1000.0 / 1000.0 / 1000.0);

    for (size_t i = 2; i < numCols - 1; ++i)
    {
        *endPointCSVContents << ",";
    }

    *endPointCSVContents << std::endl;

    return endPointCSVContents;
}
