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
#pragma once

#include "MetricsPublisher.h"
#include <atomic>
#include <aws/common/mutex.h>
#include <aws/common/string.h>
#include <aws/crt/DateTime.h>
#include <aws/crt/Types.h>
#include <aws/crt/http/HttpConnection.h>
#include <mutex>

class S3ObjectTransport;
class CanaryApp;

enum class PartFinishResponse
{
    Done,
    Retry
};

class MultipartTransferState
{
  public:
    struct PartInfo
    {
        uint32_t partIndex;
        uint32_t partNumber;
        uint64_t offsetInBytes;
        uint64_t sizeInBytes;
        uint32_t transferSuccess : 1;

        PartInfo();
        PartInfo(
            std::shared_ptr<MetricsPublisher> publisher,
            uint32_t partIndex,
            uint32_t partNumber,
            uint64_t offsetInBytes,
            uint64_t sizeInBytes);

        void AddDataUpMetric(uint64_t dataUp);

        void AddDataDownMetric(uint64_t dataDown);

        void FlushDataUpMetrics();

        void FlushDataDownMetrics();

      private:
        Aws::Crt::Vector<Metric> uploadMetrics;
        Aws::Crt::Vector<Metric> downloadMetrics;
        std::shared_ptr<MetricsPublisher> publisher;

        void DistributeDataUsedOverTime(
            Aws::Crt::Vector<Metric> &metrics,
            MetricName metricName,
            uint64_t beginTime,
            double dataUsed);

        void PushMetric(Aws::Crt::Vector<Metric> &metrics, MetricName metricName, double dataUsed);

        void PushAndTryToMerge(
            Aws::Crt::Vector<Metric> &metrics,
            MetricName metricName,
            uint64_t timestamp,
            double dataUsed);

        void FlushMetricsVector(Aws::Crt::Vector<Metric> &metrics);
    };

    using PartFinishedCallback = std::function<void(PartFinishResponse response)>;
    using ProcessPartCallback =
        std::function<void(const std::shared_ptr<PartInfo> &partInfo, PartFinishedCallback callback)>;
    using FinishedCallback = std::function<void(int32_t errorCode)>;

    MultipartTransferState(const Aws::Crt::String &key, uint64_t objectSize, uint32_t numParts);

    virtual ~MultipartTransferState();

    void SetProcessPartCallback(const ProcessPartCallback &processPartCallback);
    void SetFinishedCallback(const FinishedCallback &finishedCallback);

    void SetFinished(int32_t errorCode = AWS_ERROR_SUCCESS);
    bool IncNumPartsCompleted();

    bool IsFinished() const;
    const Aws::Crt::String &GetKey() const;
    uint32_t GetNumParts() const;
    uint32_t GetNumPartsCompleted() const;
    uint64_t GetObjectSize() const;

    template <typename... TArgs> void ProcessPart(TArgs &&... Args) const
    {
        m_processPartCallback(std::forward<TArgs>(Args)...);
    }

  private:
    int32_t m_errorCode;
    uint32_t m_numParts;
    std::atomic<bool> m_isFinished;
    std::atomic<uint32_t> m_numPartsCompleted;
    uint64_t m_objectSize;
    Aws::Crt::String m_key;
    ProcessPartCallback m_processPartCallback;
    FinishedCallback m_finishedCallback;
};

class MultipartUploadState : public MultipartTransferState
{
  public:
    MultipartUploadState(const Aws::Crt::String &key, uint64_t objectSize, uint32_t numParts);

    void SetUploadId(const Aws::Crt::String &uploadId);
    void SetETag(uint32_t partIndex, const Aws::Crt::String &etag);

    void GetETags(Aws::Crt::Vector<Aws::Crt::String> &outETags);
    const Aws::Crt::String &GetUploadId() const;

  private:
    Aws::Crt::Vector<Aws::Crt::String> m_etags;
    std::mutex m_etagsMutex;
    Aws::Crt::String m_uploadId;
};

class MultipartDownloadState : public MultipartTransferState
{
  public:
    MultipartDownloadState(const Aws::Crt::String &key, uint64_t objectSize, uint32_t numParts);
};
