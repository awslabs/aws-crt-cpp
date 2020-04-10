/*
 * Copyright 2010-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
#include <cinttypes>

#ifdef WIN32
#    undef max
#endif

using namespace Aws::Crt;

TransferState::TransferState() : m_partIndex(0), m_partNumber(0), m_sizeInBytes(0), m_transferSuccess(false) {}
TransferState::TransferState(
    std::shared_ptr<MetricsPublisher> inPublisher,
    uint32_t inPartIndex,
    uint32_t inPartNumber,
    uint64_t inSizeInBytes)
    : m_partIndex(inPartIndex), m_partNumber(inPartNumber), m_sizeInBytes(inSizeInBytes), m_transferSuccess(false),
      m_publisher(inPublisher)
{
}

void TransferState::DistributeDataUsedOverTime(
    Vector<Metric> &metrics,
    MetricName metricName,
    uint64_t beginTime,
    double dataUsed)
{
    uint64_t beginTimeSecond = beginTime / 1000ULL;
    uint64_t beginTimeSecondFrac = beginTime % 1000ULL;
    uint64_t beginTimeOneMinusSecondFrac = 1000ULL - beginTimeSecondFrac;

    uint64_t currentTicks = 0;
    aws_sys_clock_get_ticks(&currentTicks);
    uint64_t endTime = aws_timestamp_convert(currentTicks, AWS_TIMESTAMP_NANOS, AWS_TIMESTAMP_MILLIS, NULL);
    uint64_t endTimeSecond = endTime / 1000ULL;
    uint64_t endTimeSecondFrac = endTime % 1000ULL;

    AWS_FATAL_ASSERT(endTime >= beginTime);
    AWS_FATAL_ASSERT(endTimeSecond >= beginTimeSecond);

    uint64_t timeDelta = endTime - beginTime;
    uint64_t timeSecondDelta = endTimeSecond - beginTimeSecond;

    if (timeSecondDelta == 0)
    {
        PushMetricAndTryToMerge(metrics, metricName, endTime, dataUsed);
    }
    else
    {
        double beginDataUsedFraction = dataUsed * (double)(beginTimeOneMinusSecondFrac / (double)timeDelta);
        double endDataUsedFraction = dataUsed * (double)(endTimeSecondFrac / (double)timeDelta);

        PushMetricAndTryToMerge(metrics, metricName, beginTime, beginDataUsedFraction);

        if (timeSecondDelta > 1)
        {
            uint64_t interiorBeginSecond = beginTimeSecond + 1;
            uint64_t interiorEndSecond = endTimeSecond;
            uint64_t numInteriorSeconds = interiorEndSecond - interiorBeginSecond;

            AWS_FATAL_ASSERT(interiorEndSecond >= interiorBeginSecond);

            double dataUsedRemaining = dataUsed - (beginDataUsedFraction + endDataUsedFraction);
            double interiorSecondDataUsed = dataUsedRemaining / (double)numInteriorSeconds;

            for (uint64_t i = 0; i < numInteriorSeconds; ++i)
            {
                PushMetricAndTryToMerge(
                    metrics, metricName, (interiorBeginSecond * 1000ULL) + (i * 1000ULL), interiorSecondDataUsed);
            }
        }

        PushMetricAndTryToMerge(metrics, metricName, endTime, endDataUsedFraction);
    }
}

void TransferState::PushMetricAndTryToMerge(
    Vector<Metric> &metrics,
    MetricName metricName,
    uint64_t timestamp,
    double dataUsed)
{
    bool pushNew = true;
    DateTime newDateTime(timestamp);

    if (metrics.size() > 0)
    {
        Metric &lastMetric = metrics.back();
        DateTime lastDateTime(lastMetric.Timestamp);

        if (newDateTime == lastDateTime)
        {
            lastMetric.Value += dataUsed;
            lastMetric.Timestamp = std::max(lastMetric.Timestamp, timestamp);

            pushNew = false;
        }
    }

    if (pushNew)
    {
        Metric metric;
        metric.Name = metricName;
        metric.Timestamp = timestamp;
        metric.Value = dataUsed;
        metric.Unit = MetricUnit::Bytes;

        metrics.push_back(std::move(metric));
    }
}

void TransferState::PushMetric(Vector<Metric> &metrics, MetricName metricName, double dataUsed)
{
    uint64_t current_time = 0;
    aws_sys_clock_get_ticks(&current_time);
    uint64_t now = aws_timestamp_convert(current_time, AWS_TIMESTAMP_NANOS, AWS_TIMESTAMP_MILLIS, NULL);

    if (metrics.size() == 0)
    {
        Metric metric;
        metric.Name = metricName;
        metric.Timestamp = now;
        metric.Value = dataUsed;
        metric.Unit = MetricUnit::Bytes;

        metrics.push_back(metric);
    }
    else
    {
        Metric &lastMetric = metrics.back();

        DistributeDataUsedOverTime(metrics, metricName, lastMetric.Timestamp, dataUsed);
    }
}

void TransferState::FlushMetricsVector(Vector<Metric> &metrics)
{
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Adding %d data points", (uint32_t)metrics.size());

    std::shared_ptr<MetricsPublisher> publisher = m_publisher.lock();

    if (publisher != nullptr)
    {
        if (metrics.size() > 0)
        {
            Metric &metric = metrics.back();
            publisher->AddTransferStatusDataPoint(metric.Timestamp, m_transferSuccess);
        }

        publisher->AddDataPoints(metrics);

        Vector<Metric> connMetrics;

        for (Metric &metric : metrics)
        {
            connMetrics.emplace_back(MetricName::NumConnections, MetricUnit::Count, metric.Timestamp, 1.0);
        }

        publisher->AddDataPoints(connMetrics);
    }

    metrics.clear();
}

void TransferState::AddDataUpMetric(uint64_t dataUp)
{
    PushMetric(m_uploadMetrics, MetricName::BytesUp, (double)dataUp);
}

void TransferState::AddDataDownMetric(uint64_t dataDown)
{
    PushMetric(m_downloadMetrics, MetricName::BytesDown, (double)dataDown);
}

void TransferState::FlushDataUpMetrics()
{
    FlushMetricsVector(m_uploadMetrics);
}

void TransferState::FlushDataDownMetrics()
{
    FlushMetricsVector(m_downloadMetrics);
}
