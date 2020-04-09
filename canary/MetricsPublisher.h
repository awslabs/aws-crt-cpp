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

#include <aws/crt/DateTime.h>
#include <aws/crt/auth/Sigv4Signing.h>
#include <aws/crt/http/HttpConnectionManager.h>

#include <chrono>
#include <map>
#include <mutex>

enum class MetricUnit
{
    Seconds,
    Microseconds,
    Milliseconds,
    Bytes,
    Kilobytes,
    Megabytes,
    Gigabytes,
    Terabytes,
    Bits,
    Kilobits,
    Gigabits,
    Terabits,
    Percent,
    Count,
    Bytes_Per_Second,
    Kilobytes_Per_Second,
    Megabytes_Per_Second,
    Gigabytes_Per_Second,
    Terabytes_Per_Second,
    Bits_Per_Second,
    Kilobits_Per_Second,
    Megabits_Per_Second,
    Gigabits_Per_Second,
    Terabits_Per_Second,
    Counts_Per_Second,
    None,
};

enum class MetricName
{
    BytesUp,
    BytesDown,
    NumConnections,
    BytesAllocated,
    S3AddressCount,
    SuccessfulTransfer,
    FailedTransfer,
    AvgEventLoopGroupTickElapsed,
    AvgEventLoopTaskRunElapsed,
    MinEventLoopGroupTickElapsed,
    MinEventLoopTaskRunElapsed,
    MaxEventLoopGroupTickElapsed,
    MaxEventLoopTaskRunElapsed,
    NumIOSubs,

    Invalid
};

enum class MetricTransferType
{
    None,
    SinglePart,
    MultiPart,
};

enum class UploadBackupOptions
{
    PrintPath = 0x00000001
};

struct MetricKey
{
    MetricName Name;
    uint64_t TimestampSeconds;

    bool operator<(const MetricKey &otherKey) const
    {
        if (TimestampSeconds == otherKey.TimestampSeconds)
        {
            return (uint32_t)Name < (uint32_t)otherKey.Name;
        }

        return TimestampSeconds < otherKey.TimestampSeconds;
    }
};

struct Metric
{
    MetricUnit Unit;
    MetricName Name;
    uint64_t Timestamp;
    double Value;

    Metric();
    Metric(MetricName Name, MetricUnit unit, double value);
    Metric(MetricName Name, MetricUnit unit, uint64_t timestamp, double value);

    void SetTimestampNow();
};

class CanaryApp;

/**
 * Publishes an aggregated metrics collection to cloud watch at 'publishFrequency'
 */
class MetricsPublisher
{
  public:
    MetricsPublisher(
        CanaryApp &canaryApp,
        const char *metricNamespace,
        std::chrono::milliseconds publishFrequency = std::chrono::milliseconds(1000));

    ~MetricsPublisher();

    /**
     * Add a data point to the outgoing metrics collection.
     */
    void AddDataPoints(const Aws::Crt::Vector<Metric> &metricData);

    void AddDataPoint(const Metric &metricData);

    void AddTransferStatusDataPoint(uint64_t timestamp, bool transferSuccess);

    void AddTransferStatusDataPoint(bool transferSuccess);

    /*
     * Set the transfer size we are currently recording metrics for.  (Will
     * be recorded with each metric.)
     */
    void SetMetricTransferType(MetricTransferType transferType);

    void FlushMetrics();

    Aws::Crt::String UploadBackup(uint32_t options);

    void RehydrateBackup(const char *s3Path);

    /**
     * namespace to use for the metrics
     */
    Aws::Crt::Optional<Aws::Crt::String> Namespace;

  private:
    static void s_OnPublishTask(aws_task *task, void *arg, aws_task_status status);

    MetricTransferType GetTransferType() const;
    Aws::Crt::String GetPlatformName() const;
    Aws::Crt::String GetToolName() const;
    Aws::Crt::String GetInstanceType() const;
    bool IsSendingEncrypted() const;

    void SchedulePublish();

    void WaitForLastPublish();

    void WriteToBackup(const Aws::Crt::Vector<Metric> &metrics);

    void AddDataPointInternal(const Metric &newMetric);

    void PreparePayload(Aws::Crt::StringStream &bodyStream, const Aws::Crt::Vector<Metric> &metrics);

    MetricTransferType m_transferType;
    CanaryApp &m_canaryApp;
    std::shared_ptr<Aws::Crt::Http::HttpClientConnectionManager> m_connManager;
    Aws::Crt::Vector<Metric> m_publishData;
    std::map<MetricKey, size_t> m_publishDataLU;
    Aws::Crt::Vector<Metric> m_publishDataTaskCopy;
    Aws::Crt::Vector<Metric> m_metricsBackup;
    Aws::Crt::Http::HttpHeader m_hostHeader;
    Aws::Crt::Http::HttpHeader m_contentTypeHeader;
    Aws::Crt::Http::HttpHeader m_apiVersionHeader;
    Aws::Crt::String m_endpoint;
    aws_event_loop *m_schedulingLoop;
    std::mutex m_publishDataLock;
    aws_task m_publishTask;
    uint64_t m_publishFrequencyNs;
    std::condition_variable m_waitForLastPublishCV;

    Aws::Crt::Optional<MetricTransferType> m_transferTypeOverride;
    Aws::Crt::Optional<Aws::Crt::String> m_platformNameOverride;
    Aws::Crt::Optional<Aws::Crt::String> m_toolNameOverride;
    Aws::Crt::Optional<Aws::Crt::String> m_instanceTypeOverride;
    Aws::Crt::Optional<bool> m_sendEncryptedOverride;
    Aws::Crt::Optional<uint64_t> m_replayId;
};
