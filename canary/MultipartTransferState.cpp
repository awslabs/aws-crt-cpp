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

#include "MultipartTransferState.h"
#include "CanaryApp.h"
#include "MetricsPublisher.h"
#include "S3ObjectTransport.h"

#include <aws/common/clock.h>
#include <aws/common/date_time.h>
#include <aws/crt/Api.h>
#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/Stream.h>
#include <aws/io/stream.h>
#include <cinttypes>

using namespace Aws::Crt;

PartInfo::PartInfo()
    : partIndex(0), partNumber(0), sizeInBytes(0), transferSuccess(false)
{
}
PartInfo::PartInfo(
    std::shared_ptr<MetricsPublisher> inPublisher,
    uint32_t inPartIndex,
    uint32_t inPartNumber,
    uint64_t inSizeInBytes)
    : partIndex(inPartIndex), partNumber(inPartNumber), sizeInBytes(inSizeInBytes),
      transferSuccess(false), publisher(inPublisher)
{
}

void PartInfo::DistributeDataUsedOverTime(
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
        PushAndTryToMerge(metrics, metricName, endTime, dataUsed);
    }
    else
    {
        double beginDataUsedFraction = dataUsed * (double)(beginTimeOneMinusSecondFrac / (double)timeDelta);
        double endDataUsedFraction = dataUsed * (double)(endTimeSecondFrac / (double)timeDelta);

        PushAndTryToMerge(metrics, metricName, beginTime, beginDataUsedFraction);

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
                PushAndTryToMerge(
                    metrics, metricName, (interiorBeginSecond * 1000ULL) + (i * 1000ULL), interiorSecondDataUsed);
            }
        }

        PushAndTryToMerge(metrics, metricName, endTime, endDataUsedFraction);
    }
}

void PartInfo::PushAndTryToMerge(
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

void PartInfo::PushMetric(Vector<Metric> &metrics, MetricName metricName, double dataUsed)
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

void PartInfo::FlushMetricsVector(Vector<Metric> &metrics)
{
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Adding %d data points", (uint32_t)metrics.size());

    if (metrics.size() > 0)
    {
        Metric &metric = metrics.back();
        publisher->AddTransferStatusDataPoint(metric.Timestamp, transferSuccess);
    }

    publisher->AddDataPoints(metrics);

    Vector<Metric> connMetrics;

    for (Metric &metric : metrics)
    {
        connMetrics.emplace_back(MetricName::NumConnections, MetricUnit::Count, metric.Timestamp, 1.0);
    }

    publisher->AddDataPoints(connMetrics);

    metrics.clear();
}

void PartInfo::AddDataUpMetric(uint64_t dataUp)
{
    PushMetric(uploadMetrics, MetricName::BytesUp, (double)dataUp);
}

void PartInfo::AddDataDownMetric(uint64_t dataDown)
{
    PushMetric(downloadMetrics, MetricName::BytesDown, (double)dataDown);
}

void PartInfo::FlushDataUpMetrics()
{
    FlushMetricsVector(uploadMetrics);
}

void PartInfo::FlushDataDownMetrics()
{
    FlushMetricsVector(downloadMetrics);
}

MultipartTransferState::MultipartTransferState(const Aws::Crt::String &key, uint64_t objectSize, uint32_t numParts)
{
    m_isFinished = false;
    m_errorCode = AWS_ERROR_SUCCESS;
    m_numParts = numParts;
    m_numPartsCompleted = 0;
    m_objectSize = objectSize;
    m_key = key;
}

MultipartTransferState::~MultipartTransferState() {}

void MultipartTransferState::SetProcessPartCallback(const ProcessPartCallback &processPartCallback)
{
    m_processPartCallback = processPartCallback;
}

void MultipartTransferState::SetFinishedCallback(const FinishedCallback &finishedCallback)
{
    m_finishedCallback = finishedCallback;
}

void MultipartTransferState::SetFinished(int32_t errorCode)
{
    bool wasCompleted = m_isFinished.exchange(true);

    if (wasCompleted)
    {
        AWS_LOGF_INFO(
            AWS_LS_CRT_CPP_CANARY,
            "MultipartTransferState::SetCompleted being called multiple times--not recording error code %d.",
            errorCode);
    }
    else
    {
        m_errorCode = errorCode;
        m_finishedCallback(m_errorCode);
    }
}

bool MultipartTransferState::IsFinished() const
{
    return m_isFinished;
}

bool MultipartTransferState::IncNumPartsCompleted()
{
    uint32_t originalValue = m_numPartsCompleted.fetch_add(1);
    return (originalValue + 1) == GetNumParts();
}

const Aws::Crt::String &MultipartTransferState::GetKey() const
{
    return m_key;
}

uint32_t MultipartTransferState::GetNumParts() const
{
    return m_numParts;
}

uint32_t MultipartTransferState::GetNumPartsCompleted() const
{
    return m_numPartsCompleted;
}

uint64_t MultipartTransferState::GetObjectSize() const
{
    return m_objectSize;
}

MultipartUploadState::MultipartUploadState(const Aws::Crt::String &key, uint64_t objectSize, uint32_t numParts)
    : MultipartTransferState(key, objectSize, numParts)
{
    m_etags.reserve(numParts);

    for (uint32_t i = 0; i < numParts; ++i)
    {
        m_etags.push_back("");
    }
}

void MultipartUploadState::SetUploadId(const Aws::Crt::String &uploadId)
{
    m_uploadId = uploadId;
}

void MultipartUploadState::SetETag(uint32_t partIndex, const Aws::Crt::String &etag)
{
    std::lock_guard<std::mutex> lock(m_etagsMutex);

    AWS_FATAL_ASSERT(partIndex < m_etags.size());

    m_etags[partIndex] = etag;
}

const Aws::Crt::String &MultipartUploadState::GetUploadId() const
{
    return m_uploadId;
}

void MultipartUploadState::GetETags(Aws::Crt::Vector<Aws::Crt::String> &outETags)
{
    std::lock_guard<std::mutex> lock(m_etagsMutex);
    outETags = m_etags;
}

MultipartDownloadState::MultipartDownloadState(const Aws::Crt::String &key, uint64_t objectSize, uint32_t numParts)
    : MultipartTransferState(key, objectSize, numParts)
{
}
