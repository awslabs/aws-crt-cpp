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

TransferState::TransferState() : TransferState(nullptr) {}

TransferState::TransferState(const std::shared_ptr<MetricsPublisher> &publisher) : TransferState(publisher, -1) {}

TransferState::TransferState(const std::shared_ptr<MetricsPublisher> &publisher, int32_t partIndex)
    : m_partIndex(partIndex), m_transferId(TransferState::GetNextTransferId()), m_transferSuccess(false),
      m_publisher(publisher)
{
}

void TransferState::DistributeDataUsedOverSeconds(
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

    /*
     * This represents the number of second data points that this interval overlaps minus one, NOT
     * the time delta in seconds.  There are three cases that we want to detect.
     *      * If equal to 0, then endTime and beginTime are in the same second:
     *
     *          0                1                2                3
     *          |----------------|----------------|----------------|
     *             *----------*
     *          beginTime    endTime
     *
     *
     *      * If equal to 1, then endTime and beginTime are in different seconds,
     *        but do not have any full seconds in between them:
     *
     *          0                1                2                3
     *          |----------------|----------------|----------------|
     *             *--------------------------*
     *          beginTime                  endTime
     *
     *
     *      * If greater than 1, then endTime and beginTime are in different seconds,
     *        and have at least one full second inbetween them:
     *
     *          0                1                2                3
     *          |----------------|----------------|----------------|
     *             *----------------------------------------*
     *          beginTime                                endTime
     *
     */
    uint64_t numSecondsTouched = endTimeSecond - beginTimeSecond;

    if (numSecondsTouched == 0)
    {
        /*
         * This value touches only a single second, so add all "dataUsed" as a metric for this second.
         * Specifically pass endTime for this, so that any future deltas done against the last data
         * point in the metric array measures against the newest time.
         */
        PushDataUsedForSecondAndAggregate(metrics, metricName, endTime, dataUsed);
    }
    else
    {
        /*
         * This value overlaps more than a single second, so we first calculate the amount of data used
         * in the starting second and the ending second.
         */
        double beginDataUsedFraction = dataUsed * (double)(beginTimeOneMinusSecondFrac / (double)timeDelta);
        double endDataUsedFraction = dataUsed * (double)(endTimeSecondFrac / (double)timeDelta);

        // Push the data used for the beginning second.
        PushDataUsedForSecondAndAggregate(metrics, metricName, beginTime, beginDataUsedFraction);

        /*
         * In this case, we have "interior" seconds (full seconds that are overlapped), so distribute
         * data used to each of those full seconds.
         */
        if (numSecondsTouched > 1)
        {
            uint64_t interiorBeginSecond = beginTimeSecond + 1;
            uint64_t interiorEndSecond = endTimeSecond;
            uint64_t numInteriorSeconds = interiorEndSecond - interiorBeginSecond;

            AWS_FATAL_ASSERT(interiorEndSecond >= interiorBeginSecond);

            double dataUsedRemaining = dataUsed - (beginDataUsedFraction + endDataUsedFraction);
            double interiorSecondDataUsed = dataUsedRemaining / (double)numInteriorSeconds;

            for (uint64_t i = 0; i < numInteriorSeconds; ++i)
            {
                PushDataUsedForSecondAndAggregate(
                    metrics, metricName, (interiorBeginSecond * 1000ULL) + (i * 1000ULL), interiorSecondDataUsed);
            }
        }

        // Push the data used for the ending second.
        PushDataUsedForSecondAndAggregate(metrics, metricName, endTime, endDataUsedFraction);
    }
}

void TransferState::PushDataUsedForSecondAndAggregate(
    Vector<Metric> &metrics,
    MetricName metricName,
    uint64_t timestamp,
    double dataUsed)
{
    bool pushNew = true;
    DateTime newDateTime(timestamp);

    // If we already have a metric, try to aggregate the incoming data to it
    if (metrics.size() > 0)
    {
        Metric &lastMetric = metrics.back();
        DateTime lastDateTime(lastMetric.Timestamp);

        uint64_t newDateTimeSecondsSinceEpoch = newDateTime.Millis() / 1000ULL;
        uint64_t lastDateTimeSecondsSinceEpoch = lastDateTime.Millis() / 1000ULL;

        if (newDateTimeSecondsSinceEpoch == lastDateTimeSecondsSinceEpoch)
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

void TransferState::FlushMetricsVector(Vector<Metric> &metrics)
{
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Adding %d data points", (uint32_t)metrics.size());

    std::shared_ptr<MetricsPublisher> publisher = m_publisher.lock();

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

    for (Metric &metric : metrics)
    {
        connMetrics.emplace_back(MetricName::NumConnections, MetricUnit::Count, metric.Timestamp, m_transferId, 1.0);
    }

    publisher->SetTransferState(m_transferId, shared_from_this());
    publisher->AddDataPoints(connMetrics);
    publisher->AddDataPoints(metrics);

    metrics.clear();
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

void TransferState::InitDataUpMetric()
{
    AddDataUpMetric(0);
}

void TransferState::InitDataDownMetric()
{
    AddDataDownMetric(0);
}

void TransferState::AddDataUpMetric(uint64_t dataUp)
{
    PushDataMetric(m_uploadMetrics, MetricName::BytesUp, (double)dataUp);
}

void TransferState::AddDataDownMetric(uint64_t dataDown)
{
    PushDataMetric(m_downloadMetrics, MetricName::BytesDown, (double)dataDown);
}

void TransferState::FlushDataUpMetrics()
{
    FlushMetricsVector(m_uploadMetrics);
}

void TransferState::FlushDataDownMetrics()
{
    FlushMetricsVector(m_downloadMetrics);
}
