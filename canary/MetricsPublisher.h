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

enum class TransportMetricName
{
    HeldConnectionCount,
    PendingAcquisitionCount,
    PendingConnectsCount,
    VendedConnectionCount,
    OpenConnectionCount,
    FailTableCount,

    LastEnum = FailTableCount
};

enum class MetricName
{
    BytesUp,
    BytesUpFailed,
    BytesDown,
    BytesDownFailed,
    NumConnections,
    S3AddressCount,
    SuccessfulTransfer,
    FailedTransfer,

    UploadTransportMetricStart,
    UploadTransportMetricEnd = UploadTransportMetricStart + (int)TransportMetricName::LastEnum,

    DownloadTransportMetricStart,
    DownloadTransportMetricEnd = DownloadTransportMetricStart + (int)TransportMetricName::LastEnum,

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

struct Metric
{
    MetricUnit Unit;
    MetricName Name;
    uint64_t Timestamp;
    uint64_t TransferId;
    double Value;

    Metric();
    Metric(MetricName Name, MetricUnit unit, uint64_t transferId, double value);
    Metric(MetricName Name, MetricUnit unit, uint64_t timestamp, uint64_t transferId, double value);

    void SetTimestampNow();
};

class CanaryApp;
class TransferState;
class S3ObjectTransport;

/**
 * Publishes an aggregated metrics collection to CloudWatch.
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
     * Add a list of data points to the outgoing metrics collection.
     */
    void AddDataPoints(const Aws::Crt::Vector<Metric> &metricData);

    /**
     * Add a data point to the outgoing metrics collection.
     */
    void AddDataPoint(const Metric &metricData);

    /*
     * Set the transfer type we are currently recording metrics for.  (Will
     * be recorded with each metric as a dimension.)
     */
    void SetMetricTransferType(MetricTransferType transferType);

    /*
     * Sends all metrics that are currently being stored up to CloudWatch.
     */
    void FlushMetrics();

    /*
     * Upload a backup of all currently stored metrics to S3.  Returns the path
     * in S3 where the backup is stored.
     */
    Aws::Crt::String UploadBackup(uint32_t options);

    /*
     * Given a path to a metrics backup in S3, this will republish those metrics
     * to S3 with an id to keep it from conflicting with other metrics.
     */
    void RehydrateBackup(const char *s3Path);

    void SetTransferState(uint64_t transferId, const std::shared_ptr<TransferState> &transferState);

  private:
    struct AggregateMetricKey
    {
        MetricName Name;
        uint64_t GroupId;
        uint64_t TimestampSeconds;

        AggregateMetricKey() : Name(MetricName::Invalid), GroupId(0ULL), TimestampSeconds(0ULL) {}

        AggregateMetricKey(MetricName name, uint64_t groupId, uint64_t timestampSeconds)
            : Name(name), GroupId(groupId), TimestampSeconds(timestampSeconds)
        {
        }

        AggregateMetricKey(MetricName name, uint64_t timestampSeconds)
            : Name(name), GroupId(0), TimestampSeconds(timestampSeconds)
        {
        }

        bool operator<(const AggregateMetricKey &otherKey) const
        {
            if (TimestampSeconds == otherKey.TimestampSeconds)
            {
                if (GroupId == otherKey.GroupId)
                {
                    return (uint32_t)Name < (uint32_t)otherKey.Name;
                }
                else
                {
                    return GroupId < otherKey.GroupId;
                }
            }

            return TimestampSeconds < otherKey.TimestampSeconds;
        }
    };

    static void s_OnPollingTask(aws_task *task, void *arg, aws_task_status status);
    static void s_OnPublishTask(aws_task *task, void *arg, aws_task_status status);

    void PollMetricsForS3ObjectTransport(
        const std::shared_ptr<S3ObjectTransport> &transport,
        uint32_t metricNameOffset);

    MetricTransferType GetTransferType() const;
    Aws::Crt::String GetPlatformName() const;
    Aws::Crt::String GetToolName() const;
    Aws::Crt::String GetInstanceType() const;
    bool IsSendingEncrypted() const;
    Aws::Crt::String CreateUUID() const;

    void AggregateDataPoints(
        const Aws::Crt::Vector<Metric> &dataPoints,
        Aws::Crt::Map<AggregateMetricKey, size_t> &aggregateLU,
        Aws::Crt::Vector<Metric> &aggregateDataPoints,
        bool useTransferId);

    double GetAggregateDataPoint(
        const AggregateMetricKey &key,
        const Aws::Crt::Map<AggregateMetricKey, size_t> &aggregateLU,
        const Aws::Crt::Vector<Metric> &aggregateDataPoints,
        bool *outKeyExists = nullptr);

    void SchedulePolling();

    void SchedulePublish();

    void WaitForLastPublish();

    void AddDataPointInternal(const Metric &newMetric);

    void PreparePayload(Aws::Crt::StringStream &bodyStream, const Aws::Crt::Vector<Metric> &metrics);

    std::shared_ptr<Aws::Crt::StringStream> GenerateMetricsBackupJson();

    Aws::Crt::String GetTimeString(uint64_t timestampSeconds) const;

    std::shared_ptr<Aws::Crt::StringStream> GeneratePerStreamCSV(
        MetricName transferMetricName,
        const Aws::Crt::Map<AggregateMetricKey, size_t> &aggregateDataPointsByGroupLU,
        const Aws::Crt::Vector<Metric> &aggregateDataPointsByGroup);

    void GeneratePerStreamCSVRow(
        uint64_t groupId,
        uint64_t timestampStart,
        uint64_t timestampEnd,
        MetricName dataTransferMetric,
        const Aws::Crt::Map<AggregateMetricKey, size_t> &aggregateDataPointsLU,
        const Aws::Crt::Vector<Metric> &aggregateDataPoints,
        Aws::Crt::Vector<Aws::Crt::String> &streamStringValues,
        Aws::Crt::Vector<double> &streamNumericValues,
        Aws::Crt::Vector<Aws::Crt::String> &overallStringValues,
        Aws::Crt::Vector<double> &overallNumericValues);

    void WritePerStreamCSVRowHeader(
        const std::shared_ptr<Aws::Crt::StringStream> &csvContents,
        uint64_t timestampStartSeconds,
        uint64_t timestampEndSeconds);

    void WritePerStreamCSVRow(
        const std::shared_ptr<Aws::Crt::StringStream> &csvContents,
        const Aws::Crt::Vector<Aws::Crt::String> &stringValues,
        const Aws::Crt::Vector<double> &numericValues);

    CanaryApp &m_canaryApp;
    MetricTransferType m_transferType;
    Aws::Crt::Optional<Aws::Crt::String> m_metricNamespace;
    std::shared_ptr<Aws::Crt::Http::HttpClientConnectionManager> m_connManager;

    std::mutex m_dataPointsLock;
    Aws::Crt::Vector<Metric> m_dataPointsPending;
    Aws::Crt::Vector<Aws::Crt::Vector<Metric>> m_dataPointsPublished;

    std::mutex m_transferIdToStateLock;
    Aws::Crt::Map<uint64_t, std::shared_ptr<TransferState>> m_transferIdToState;

    Aws::Crt::Http::HttpHeader m_hostHeader;
    Aws::Crt::Http::HttpHeader m_contentTypeHeader;
    Aws::Crt::Http::HttpHeader m_apiVersionHeader;

    Aws::Crt::String m_endpoint;
    aws_event_loop *m_schedulingLoop;

    aws_task m_pollingTask;
    uint64_t m_pollingFrequencyNs;
    std::atomic<uint32_t> m_pollingFinishState;

    std::mutex m_publishDataLock;
    Aws::Crt::Vector<Metric> m_publishData;
    Aws::Crt::Vector<Metric> m_publishDataTaskCopy;
    aws_task m_publishTask;
    uint64_t m_publishFrequencyNs;
    std::condition_variable m_waitForLastPublishCV;

    Aws::Crt::Optional<MetricTransferType> m_transferTypeOverride;
    Aws::Crt::Optional<Aws::Crt::String> m_platformNameOverride;
    Aws::Crt::Optional<Aws::Crt::String> m_toolNameOverride;
    Aws::Crt::Optional<Aws::Crt::String> m_instanceTypeOverride;
    Aws::Crt::Optional<bool> m_sendEncryptedOverride;
    Aws::Crt::Optional<Aws::Crt::String> m_replayId;
};
