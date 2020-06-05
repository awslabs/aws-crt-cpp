/*
 * Copyright 2010-2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */

#include "TransferState.h"

#include <aws/common/clock.h>
#include <aws/common/date_time.h>
#include <aws/crt/Api.h>
#include <aws/crt/io/EndPointMonitor.h>
#include <aws/http/connection.h>
#include <cinttypes>

#ifdef WIN32
#    undef max
#endif

using namespace Aws::Crt;

std::atomic<uint64_t> TransferState::s_nextTransferId(1ULL);

uint64_t TransferState::GetNextTransferId()
{
    return s_nextTransferId.fetch_add(1) + 1;
}

TransferState::TransferState() : TransferState(-1) {}

TransferState::TransferState(int32_t partIndex)
    : m_partIndex(partIndex), m_transferId(TransferState::GetNextTransferId()), m_transferSuccess(false)
{
}

void TransferState::DistributeDataUsedOverSeconds(
    Vector<Metric> &metrics,
    MetricName metricName,
    uint64_t beginTime,
    double dataUsed)
{
    uint64_t currentTicks = 0;
    aws_sys_clock_get_ticks(&currentTicks);
    uint64_t endTime = aws_timestamp_convert(currentTicks, AWS_TIMESTAMP_NANOS, AWS_TIMESTAMP_MILLIS, NULL);

    AWS_FATAL_ASSERT(endTime >= beginTime);

    uint64_t totalTimeDelta = endTime - beginTime;
    uint64_t currentTime = beginTime;

    if (totalTimeDelta == 0ULL)
    {
        PushDataUsedForSecondAndAggregate(metrics, metricName, endTime, dataUsed);
    }
    else
    {
        while (currentTime != endTime)
        {
            uint64_t timeFraction = currentTime % 1000ULL;
            uint64_t nextTime = currentTime + (1000ULL - timeFraction);
            nextTime = std::min(nextTime, endTime);

            uint64_t timeInterval = nextTime - currentTime;
            double dataUsedFrac = dataUsed * ((double)timeInterval / (double)totalTimeDelta);

            PushDataUsedForSecondAndAggregate(metrics, metricName, nextTime, dataUsedFrac);

            currentTime = nextTime;
        }
    }
}

void TransferState::PushDataUsedForSecondAndAggregate(
    Vector<Metric> &metrics,
    MetricName metricName,
    uint64_t timestamp,
    double dataUsed)
{
    bool pushNew = true;

    // If we already have a metric, try to aggregate the incoming data to it
    if (metrics.size() > 0)
    {
        Metric &lastMetric = metrics.back();

        uint64_t newTimestampSeconds = timestamp / 1000ULL;
        uint64_t lastMetricTimestampSeconds = lastMetric.Timestamp / 1000ULL;

        if (newTimestampSeconds == lastMetricTimestampSeconds)
        {
            lastMetric.Value += dataUsed;
            lastMetric.Timestamp = std::max(lastMetric.Timestamp, timestamp);

            pushNew = false;
        }
    }

    if (pushNew)
    {
        metrics.emplace_back(metricName, MetricUnit::Bytes, timestamp, m_transferId, dataUsed);
    }
}

void TransferState::PushDataMetric(Vector<Metric> &metrics, MetricName metricName, double dataUsed)
{
    uint64_t current_time = 0;
    aws_sys_clock_get_ticks(&current_time);
    uint64_t now = aws_timestamp_convert(current_time, AWS_TIMESTAMP_NANOS, AWS_TIMESTAMP_MILLIS, NULL);

    if (metrics.size() == 0)
    {
        Metric metric(metricName, MetricUnit::Bytes, now, m_transferId, dataUsed);
        metrics.push_back(metric);
    }
    else
    {
        Metric &lastMetric = metrics.back();

        DistributeDataUsedOverSeconds(metrics, metricName, lastMetric.Timestamp, dataUsed);
    }
}

void TransferState::FlushMetricsVector(const std::shared_ptr<MetricsPublisher> &publisher, Vector<Metric> &metrics)
{
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Adding %d data points", (uint32_t)metrics.size());

    AWS_FATAL_ASSERT(publisher != nullptr);

    if (metrics.size() > 0)
    {
        Metric &lastMetric = metrics.back();

        Metric transferStatusMetric(
            m_transferSuccess ? MetricName::SuccessfulTransfer : MetricName::FailedTransfer,
            MetricUnit::Count,
            lastMetric.Timestamp,
            m_transferId,
            1.0);

        publisher->AddDataPoint(transferStatusMetric);
    }

    Vector<Metric> connMetrics;

    uint64_t minTimestamp = ~0ULL;
    uint64_t maxTimestamp = 0ULL;

    for (Metric &metric : metrics)
    {
        minTimestamp = std::min(metric.Timestamp, minTimestamp);
        maxTimestamp = std::max(metric.Timestamp, maxTimestamp);
    }

    if (minTimestamp < maxTimestamp)
    {
        uint64_t startSec = minTimestamp / 1000ULL;
        uint64_t endSec = maxTimestamp / 1000ULL;

        for (uint64_t i = startSec; i <= endSec; ++i)
        {
            connMetrics.emplace_back(MetricName::NumConnections, MetricUnit::Count, i * 1000ULL, m_transferId, 1.0);
        }
    }

    publisher->SetTransferState(m_transferId, shared_from_this());
    publisher->AddDataPoints(connMetrics);
    publisher->AddDataPoints(metrics);

    if (!m_transferSuccess)
    {
        size_t i = 0;

        while (i < metrics.size())
        {
            Metric &metric = metrics[i];

            if (metric.Name == MetricName::BytesUp)
            {
                metric.Name = MetricName::BytesUpFailed;
                ++i;
            }
            else if (metric.Name == MetricName::BytesDown)
            {
                metric.Name = MetricName::BytesDownFailed;
                ++i;
            }
            else
            {
                metrics[i] = metrics.back();
                metrics.pop_back();
            }
        }

        publisher->AddDataPoints(metrics);
    }

    metrics.clear();
}

void TransferState::ResetRateTracking()
{
    uint64_t current_time = 0;
    aws_sys_clock_get_ticks(&current_time);
    uint64_t now = aws_timestamp_convert(current_time, AWS_TIMESTAMP_NANOS, AWS_TIMESTAMP_MILLIS, NULL);

    m_dataUsedRateTimestamp = now;
    m_dataUsedRateSum = 0;
}

void TransferState::UpdateRateTracking(uint64_t dataUsed, bool forceFlush)
{
    uint64_t current_time = 0;
    aws_sys_clock_get_ticks(&current_time);
    uint64_t now = aws_timestamp_convert(current_time, AWS_TIMESTAMP_NANOS, AWS_TIMESTAMP_MILLIS, NULL);

    m_dataUsedRateSum += dataUsed;

    uint64_t dataUsedRateTimeInterval = now - m_dataUsedRateTimestamp;

    if ((forceFlush && m_dataUsedRateSum > 0ULL) || dataUsedRateTimeInterval > 1000ULL)
    {
        double dataUsedRateTimeIntervalSeconds = (double)dataUsedRateTimeInterval / 1000.0;

        uint64_t perSecondRate = (uint64_t)((double)m_dataUsedRateSum / dataUsedRateTimeIntervalSeconds);

        std::shared_ptr<Http::HttpClientConnection> connection = m_connection.lock();

        if (connection == nullptr)
        {
            AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "TransferState::UpdateRateTracking - Attached connection is null.");
        }
        else
        {
            Io::EndPointMonitor *monitor =
                (Io::EndPointMonitor *)aws_http_connection_get_endpoint_monitor(connection->GetUnderlyingHandle());

            if (monitor != nullptr)
            {
                monitor->AddSample(perSecondRate);
            }
            else
            {
                //    AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "TransferState::UpdateRateTracking - Attached monitor is
                //    null.");
            }
        }

        m_dataUsedRateTimestamp = now;
        m_dataUsedRateSum = 0;
    }
}

void TransferState::ProcessHeaders(const Http::HttpHeader *headersArray, size_t headersCount)
{
    for (size_t i = 0; i < headersCount; ++i)
    {
        const Http::HttpHeader *header = &headersArray[i];
        const aws_byte_cursor &headerName = header->name;

        if (aws_byte_cursor_eq_c_str(&headerName, "x-amz-request-id"))
        {
            const aws_byte_cursor &value = header->value;
            m_amzRequestId = String((const char *)value.ptr, value.len);
        }
        else if (aws_byte_cursor_eq_c_str(&headerName, "x-amz-id-2"))
        {
            const aws_byte_cursor &value = header->value;
            m_amzId2 = String((const char *)value.ptr, value.len);
        }
    }
}

void TransferState::SetConnection(const std::shared_ptr<Aws::Crt::Http::HttpClientConnection> &connection)
{
    m_connection = connection;

    if (connection == nullptr)
    {
        return;
    }

    m_hostAddress = connection->GetHostAddress();

    Io::EndPointMonitor *endPointMonitor =
        (Io::EndPointMonitor *)aws_http_connection_get_endpoint_monitor(connection->GetUnderlyingHandle());

    if (endPointMonitor != nullptr && endPointMonitor->IsInFailTable())
    {
        AWS_LOGF_INFO(
            AWS_LS_CRT_CPP_CANARY,
            "TransferState::SetConnection - Connection being set on transfer state that has a fail-listed endpoint.");
    }
}

void TransferState::SetTransferSuccess(bool success)
{
    m_transferSuccess = success;

    UpdateRateTracking(0ULL, true);

    std::shared_ptr<Http::HttpClientConnection> connection = GetConnection();

    if (connection == nullptr)
    {
        AWS_LOGF_ERROR(
            AWS_LS_CRT_CPP_CANARY,
            "TransferState::SetTransferSuccess - No connection currently exists for TransferState");
        return;
    }

    Io::EndPointMonitor *endPointMonitor =
        connection != nullptr
            ? (Io::EndPointMonitor *)aws_http_connection_get_endpoint_monitor(connection->GetUnderlyingHandle())
            : nullptr;

    if (endPointMonitor == nullptr)
    {
        AWS_LOGF_INFO(
            AWS_LS_CRT_CPP_CANARY,
            "TransferState::SetTransferSuccess - No Endpoint Monitor currently exists for TransferState");
        return;
    }

    if (endPointMonitor->IsInFailTable())
    {
        AWS_LOGF_INFO(
            AWS_LS_CRT_CPP_CANARY,
            "TransferState::SetTransferSuccess - Cnnection's endpoint is in the fail table, force closing connection.");

        // Force connection to close so that it doesn't go back into the pool.
        connection->Close();
    }
}

void TransferState::InitDataUpMetric()
{
    ResetRateTracking();

    AddDataUpMetric(0);
}

void TransferState::InitDataDownMetric()
{
    ResetRateTracking();

    AddDataDownMetric(0);
}

void TransferState::AddDataUpMetric(uint64_t dataUp)
{
    UpdateRateTracking(dataUp, false);

    PushDataMetric(m_uploadMetrics, MetricName::BytesUp, (double)dataUp);
}

void TransferState::AddDataDownMetric(uint64_t dataDown)
{
    UpdateRateTracking(dataDown, false);

    PushDataMetric(m_downloadMetrics, MetricName::BytesDown, (double)dataDown);
}

void TransferState::FlushDataUpMetrics(const std::shared_ptr<MetricsPublisher> &publisher)
{
    FlushMetricsVector(publisher, m_uploadMetrics);
}

void TransferState::FlushDataDownMetrics(const std::shared_ptr<MetricsPublisher> &publisher)
{
    FlushMetricsVector(publisher, m_downloadMetrics);
}
