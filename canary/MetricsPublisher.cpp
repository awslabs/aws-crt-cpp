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

#include "MetricsPublisher.h"
#include "CanaryApp.h"
#include "MeasureTransferRate.h"
#include "S3ObjectTransport.h"

#include <aws/crt/JsonObject.h>
#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/http/HttpRequestResponse.h>

#include <aws/common/clock.h>
#include <aws/common/task_scheduler.h>
#include <aws/common/uuid.h>
#include <condition_variable>
#include <inttypes.h>
#include <iomanip>
#include <iostream>
#include <time.h>

#ifdef WIN32
#    undef min
#    undef max
#endif

using namespace Aws::Crt;

namespace
{
    enum class PerStreamCSVColumn
    {
        RowId,
        Success,
        Failed,
        SuccessTime,
        FailedTime,
        ThroughputStart
    };

    const char *S3BackupBucket = "aws-crt-canary-bucket";

    const char *MetricUnitStr[] = {"Seconds",
                                   "Microseconds",
                                   "Milliseconds",
                                   "Bytes",
                                   "Kilobytes",
                                   "Megabytes",
                                   "Gigabytes",
                                   "Terabytes",
                                   "Bits",
                                   "Kilobits",
                                   "Gigabits",
                                   "Terabits",
                                   "Percent",
                                   "Count",
                                   "Bytes%2FSecond",
                                   "Kilobytes%2FSecond",
                                   "Megabytes%2FSecond",
                                   "Gigabytes%2FSecond",
                                   "Terabytes%2FSecond",
                                   "Bits%2FSecond",
                                   "Kilobits%2FSecond",
                                   "Megabits%2FSecond",
                                   "Gigabits%2FSecond",
                                   "Terabits%2FSecond",
                                   "Counts%2FSecond",
                                   "None"};

    const char *MetricNameStr[] =
        {"BytesUp", "BytesDown", "NumConnections", "S3AddressCount", "SuccessfulTransfer", "FailedTransfer", "Invalid"};

    const char *TransferTypeStr[] = {"None", "SinglePart", "MultiPart"};

    const char *UnitToStr(MetricUnit unit)
    {
        auto index = static_cast<size_t>(unit);
        if (index >= AWS_ARRAY_SIZE(MetricUnitStr))
        {
            return "None";
        }
        return MetricUnitStr[index];
    }

    MetricUnit StringToMetricUnit(const char *str)
    {
        for (size_t i = 0; i < AWS_ARRAY_SIZE(MetricUnitStr); ++i)
        {
            if (!strcmp(str, MetricUnitStr[i]))
            {
                return (MetricUnit)i;
            }
        }

        return MetricUnit::None;
    }

    const char *MetricNameToStr(MetricName name)
    {
        auto index = static_cast<size_t>(name);
        if (index >= AWS_ARRAY_SIZE(MetricNameStr))
        {
            return "None";
        }
        return MetricNameStr[index];
    }

    MetricName StringToMetricName(const char *str)
    {
        for (size_t i = 0; i < AWS_ARRAY_SIZE(MetricNameStr); ++i)
        {
            if (!strcmp(str, MetricNameStr[i]))
            {
                return (MetricName)i;
            }
        }

        return MetricName::Invalid;
    }

    const char *MetricTransferTypeToString(MetricTransferType transferType)
    {
        auto index = static_cast<size_t>(transferType);
        if (index >= AWS_ARRAY_SIZE(TransferTypeStr))
        {
            return "None";
        }
        return TransferTypeStr[index];
    }

    MetricTransferType StringToMetricTransferType(const char *str)
    {
        for (size_t i = 0; i < AWS_ARRAY_SIZE(TransferTypeStr); ++i)
        {
            if (!strcmp(str, TransferTypeStr[i]))
            {
                return (MetricTransferType)i;
            }
        }

        return MetricTransferType::None;
    }
} // namespace

Metric::Metric() {}

Metric::Metric(MetricName name, MetricUnit unit, uint64_t transferId, double value)
    : Unit(unit), Name(name), TransferId(transferId), Value(value)
{
    SetTimestampNow();
}

Metric::Metric(MetricName name, MetricUnit unit, uint64_t timestamp, uint64_t transferId, double value)
    : Unit(unit), Name(name), Timestamp(timestamp), TransferId(transferId), Value(value)
{
}

void Metric::SetTimestampNow()
{
    uint64_t current_time = 0;
    aws_sys_clock_get_ticks(&current_time);
    Timestamp = (time_t)aws_timestamp_convert(current_time, AWS_TIMESTAMP_NANOS, AWS_TIMESTAMP_MILLIS, NULL);
}

MetricsPublisher::MetricsPublisher(
    CanaryApp &canaryApp,
    const char *metricNamespace,
    std::chrono::milliseconds publishFrequency)
    : m_canaryApp(canaryApp)
{
    m_metricNamespace = metricNamespace;

    AWS_ZERO_STRUCT(m_publishTask);
    m_publishFrequencyNs =
        aws_timestamp_convert(publishFrequency.count(), AWS_TIMESTAMP_MILLIS, AWS_TIMESTAMP_NANOS, NULL);
    m_publishTask.fn = MetricsPublisher::s_OnPublishTask;
    m_publishTask.arg = this;

    Http::HttpClientConnectionManagerOptions connectionManagerOptions;
    m_endpoint = String() + "monitoring." + canaryApp.GetOptions().region.c_str() + ".amazonaws.com";

    connectionManagerOptions.ConnectionOptions.HostName = m_endpoint;
    connectionManagerOptions.ConnectionOptions.Port = 443;
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetConnectTimeoutMs(3000);
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetSocketType(AWS_SOCKET_STREAM);
    connectionManagerOptions.ConnectionOptions.InitialWindowSize = SIZE_MAX;

    aws_byte_cursor serverName = ByteCursorFromCString(connectionManagerOptions.ConnectionOptions.HostName.c_str());

    auto connOptions = canaryApp.GetTlsContext().NewConnectionOptions();
    connOptions.SetServerName(serverName);
    connectionManagerOptions.ConnectionOptions.TlsOptions = connOptions;
    connectionManagerOptions.ConnectionOptions.Bootstrap = &canaryApp.GetBootstrap();
    connectionManagerOptions.MaxConnections = 5;

    m_connManager =
        Http::HttpClientConnectionManager::NewClientConnectionManager(connectionManagerOptions, g_allocator);

    m_schedulingLoop = aws_event_loop_group_get_next_loop(canaryApp.GetEventLoopGroup().GetUnderlyingHandle());
    // SchedulePublish();

    m_hostHeader.name = ByteCursorFromCString("host");
    m_hostHeader.value = ByteCursorFromCString(m_endpoint.c_str());

    m_contentTypeHeader.name = ByteCursorFromCString("content-type");
    m_contentTypeHeader.value = ByteCursorFromCString("application/x-www-form-urlencoded");

    m_apiVersionHeader.name = ByteCursorFromCString("x-amz-api-version");
    m_apiVersionHeader.value = ByteCursorFromCString("2011-06-15");
}

MetricsPublisher::~MetricsPublisher()
{
    aws_event_loop_cancel_task(m_schedulingLoop, &m_publishTask);
}

MetricTransferType MetricsPublisher::GetTransferType() const
{
    return m_transferTypeOverride.has_value() ? m_transferTypeOverride.value() : m_transferType;
}

String MetricsPublisher::GetPlatformName() const
{
    return m_platformNameOverride.has_value() ? m_platformNameOverride.value()
                                              : String(m_canaryApp.GetOptions().platformName.c_str());
}

String MetricsPublisher::GetToolName() const
{
    return m_toolNameOverride.has_value() ? m_toolNameOverride.value()
                                          : String(m_canaryApp.GetOptions().toolName.c_str());
}

String MetricsPublisher::GetInstanceType() const
{
    return m_instanceTypeOverride.has_value() ? m_instanceTypeOverride.value()
                                              : String(m_canaryApp.GetOptions().instanceType.c_str());
}

bool MetricsPublisher::IsSendingEncrypted() const
{
    return m_sendEncryptedOverride.has_value() ? m_sendEncryptedOverride.value()
                                               : m_canaryApp.GetOptions().sendEncrypted;
}

String MetricsPublisher::CreateUUID() const
{
    char uuid_storage[AWS_UUID_STR_LEN] = {0};
    struct aws_byte_buf uuid_buf = aws_byte_buf_from_empty_array(uuid_storage, sizeof(uuid_storage));

    struct aws_uuid uuid;
    AWS_FATAL_ASSERT(aws_uuid_init(&uuid) == AWS_OP_SUCCESS);
    AWS_FATAL_ASSERT(aws_uuid_to_str(&uuid, &uuid_buf) == AWS_OP_SUCCESS);

    return String(uuid_storage);
}

void MetricsPublisher::FlushMetrics()
{
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Flushing metrics...");

    Map<AggregateMetricKey, size_t> aggregateLU;
    Vector<Metric> aggregatedDataPoints;

    {
        std::lock_guard<std::mutex> lock(m_dataPointsLock);

        // Aggregate data points where possible, just to minimize publish time to CloudWatch
        AggregateDataPoints(m_dataPointsPending, aggregateLU, aggregatedDataPoints, false);

        // Record what data points were just published for later use.
        m_dataPointsPublished.emplace_back(std::move(m_dataPointsPending));
    }

    {
        std::lock_guard<std::mutex> lock(m_publishDataLock);

        // Copy aggregated metrics to be published.
        for (const Metric &metric : aggregatedDataPoints)
        {
            m_publishData.push_back(metric);
        }
    }

    SchedulePublish();
    WaitForLastPublish();
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Metrics flushed.");
}

void MetricsPublisher::AggregateDataPoints(
    const Vector<Metric> &dataPoints,
    Map<AggregateMetricKey, size_t> &aggregateLU,
    Vector<Metric> &agreggatedDataPoints,
    bool useTransferId)
{
    for (const Metric &metric : dataPoints)
    {
        AggregateMetricKey key;
        key.Name = metric.Name;
        key.GroupId = useTransferId ? metric.TransferId : 0ULL;
        key.TimestampSeconds = metric.Timestamp / 1000ULL;

        auto it = aggregateLU.find(key);

        if (it != aggregateLU.end())
        {
            size_t index = it->second;

            Metric &existingMetric = agreggatedDataPoints[index];
            existingMetric.Value += metric.Value;
        }
        else
        {
            agreggatedDataPoints.push_back(metric);

            std::pair<AggregateMetricKey, size_t> keyValue(key, agreggatedDataPoints.size() - 1);

            aggregateLU.insert(keyValue);
        }
    }
}

double MetricsPublisher::GetAggregateDataPoint(
    const AggregateMetricKey &key,
    const Map<AggregateMetricKey, size_t> &aggregateLU,
    const Vector<Metric> &aggregateDataPoints)
{
    auto it = aggregateLU.find(key);

    if (it == aggregateLU.end())
    {
        return 0.0;
    }

    return aggregateDataPoints[it->second].Value;
}

void MetricsPublisher::SchedulePublish()
{
    uint64_t now = 0;
    aws_event_loop_current_clock_time(m_schedulingLoop, &now);
    aws_event_loop_schedule_task_future(m_schedulingLoop, &m_publishTask, now + m_publishFrequencyNs);
}

void MetricsPublisher::SetMetricTransferType(MetricTransferType transferType)
{
    m_transferType = transferType;
}

void MetricsPublisher::PreparePayload(StringStream &bodyStream, const Vector<Metric> &metrics)
{
    bodyStream << "Action=PutMetricData&";

    if (m_metricNamespace)
    {
        bodyStream << "Namespace=" << *m_metricNamespace << "&";
    }

    String transferTypeString = MetricTransferTypeToString(GetTransferType());
    String platformName = GetPlatformName();
    String toolName = GetToolName();
    String instanceType = GetInstanceType();
    bool encrypted = IsSendingEncrypted();

    size_t metricCount = 1;

    for (const Metric &metric : metrics)
    {
        bodyStream << "MetricData.member." << metricCount << ".MetricName=" << MetricNameToStr(metric.Name) << "&";
        uint8_t dateBuffer[AWS_DATE_TIME_STR_MAX_LEN];
        AWS_ZERO_ARRAY(dateBuffer);
        auto dateBuf = ByteBufFromEmptyArray(dateBuffer, AWS_ARRAY_SIZE(dateBuffer));
        DateTime metricDateTime(metric.Timestamp);
        metricDateTime.ToGmtString(DateFormat::ISO_8601, dateBuf);
        String dateStr((char *)dateBuf.buffer, dateBuf.len);

        bodyStream << "MetricData.member." << metricCount << ".Timestamp=" << dateStr << "&";
        bodyStream.precision(17);
        bodyStream << "MetricData.member." << metricCount << ".Value=" << std::fixed << metric.Value << "&";
        bodyStream << "MetricData.member." << metricCount << ".Unit=" << UnitToStr(metric.Unit) << "&";

        bodyStream << "MetricData.member." << metricCount << ".StorageResolution=1&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.1.Name=Platform&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.1.Value=" << platformName << "&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.2.Name=ToolName&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.2.Value=" << toolName << "&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.3.Name=InstanceType&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.3.Value=" << instanceType << "&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.4.Name=TransferType&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.4.Value=" << transferTypeString << "&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.5.Name=Encrypted&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.5.Value=" << encrypted << "&";

        if (m_replayId.has_value())
        {
            bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.6.Name=ReplayId&";
            bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.6.Value=" << m_replayId.value()
                       << "&";
        }

        metricCount++;
    }

    bodyStream << "Version=2010-08-01";
}

String MetricsPublisher::GetTimeString(uint64_t timestampSeconds) const
{
    uint64_t timestampMillis = timestampSeconds * 1000ULL;
    DateTime dateTime(timestampMillis);

    StringStream dateTimeString;
    dateTimeString << std::setfill('0') << std::setw(2) << (uint32_t)dateTime.GetHour() << ":" << std::setw(2)
                   << (uint32_t)dateTime.GetMinute() << ":" << std::setw(2) << (uint32_t)dateTime.GetSecond();

    return dateTimeString.str();
}

void MetricsPublisher::WritePerStreamCSVRowHeader(
    const std::shared_ptr<StringStream> &csvContents,
    uint64_t timestampStartSeconds,
    uint64_t timestampEndSeconds)
{
    *csvContents << "Stream Id,Success,Failed,Success Time,Failed Time";

    uint64_t numSeconds = timestampEndSeconds - timestampStartSeconds;

    for (uint64_t i = 0ULL; i <= numSeconds; ++i)
    {
        String relativeTimeString = GetTimeString(i);

        *csvContents << "," << relativeTimeString;
    }

    *csvContents << "\n";
}

void MetricsPublisher::GeneratePerStreamCSVRow(
    uint64_t groupId,
    uint64_t timestampStart,
    uint64_t timestampEnd,
    MetricName dataTransferMetric,
    const Map<AggregateMetricKey, size_t> &aggregateDataPointsLU,
    const Vector<Metric> &aggregateDataPoints,
    Vector<double> &streamRow,
    Vector<double> &overallRow)
{
    streamRow[(size_t)PerStreamCSVColumn::RowId] = (double)groupId;

    for (uint64_t timestampSeconds = timestampStart; timestampSeconds <= timestampEnd; ++timestampSeconds)
    {
        AggregateMetricKey streamBytesKey(dataTransferMetric, groupId, timestampSeconds);
        AggregateMetricKey streamSuccessKey(MetricName::SuccessfulTransfer, groupId, timestampSeconds);
        AggregateMetricKey streamFailedKey(MetricName::FailedTransfer, groupId, timestampSeconds);

        double streamBytes = GetAggregateDataPoint(streamBytesKey, aggregateDataPointsLU, aggregateDataPoints);
        double streamSuccess = GetAggregateDataPoint(streamSuccessKey, aggregateDataPointsLU, aggregateDataPoints);
        double streamFailed = GetAggregateDataPoint(streamFailedKey, aggregateDataPointsLU, aggregateDataPoints);

        double streamGigabits = streamBytes * 8.0 / 1000.0 / 1000.0 / 1000.0;

        double relativeTimestamp = (double)(timestampSeconds - timestampStart);
        size_t csvThroughputIndex = (size_t)PerStreamCSVColumn::ThroughputStart + (size_t)relativeTimestamp;

        if (streamSuccess > 0.0)
        {
            streamRow[(size_t)PerStreamCSVColumn::Success] = streamSuccess;
            streamRow[(size_t)PerStreamCSVColumn::SuccessTime] = relativeTimestamp;

            overallRow[(size_t)PerStreamCSVColumn::Success] += streamSuccess;
            overallRow[(size_t)PerStreamCSVColumn::SuccessTime] = relativeTimestamp;
        }

        if (streamFailed > 0.0)
        {
            streamRow[(size_t)PerStreamCSVColumn::Failed] = streamFailed;
            streamRow[(size_t)PerStreamCSVColumn::FailedTime] = relativeTimestamp;

            overallRow[(size_t)PerStreamCSVColumn::Failed] += streamFailed;
            overallRow[(size_t)PerStreamCSVColumn::FailedTime] = relativeTimestamp;
        }

        streamRow[csvThroughputIndex] = streamGigabits;
        overallRow[csvThroughputIndex] += streamGigabits;
    }
}

void MetricsPublisher::WritePerStreamCSVRow(const std::shared_ptr<StringStream> &csvContents, const Vector<double> &row)
{
    uint64_t rowId = (uint64_t)row[(size_t)PerStreamCSVColumn::RowId];

    if (rowId == 0)
    {
        *csvContents << "Totals,";
    }
    else
    {
        *csvContents << rowId << ",";
    }

    *csvContents << (uint64_t)row[(size_t)PerStreamCSVColumn::Success] << ","
                 << (uint64_t)row[(size_t)PerStreamCSVColumn::Failed] << ","
                 << GetTimeString((uint64_t)row[(size_t)PerStreamCSVColumn::SuccessTime]) << ","
                 << GetTimeString((uint64_t)row[(size_t)PerStreamCSVColumn::FailedTime]);

    for (size_t i = (size_t)PerStreamCSVColumn::ThroughputStart; i < row.size(); ++i)
    {
        *csvContents << "," << row[i];
    }

    *csvContents << "\n";
}

std::shared_ptr<StringStream> MetricsPublisher::GeneratePerStreamCSV(
    MetricName transferMetricName,
    const Map<AggregateMetricKey, size_t> &aggregateDataPointsLU,
    const Vector<Metric> &aggregateDataPoints)
{
    std::shared_ptr<StringStream> csvContents = MakeShared<StringStream>(g_allocator);
    std::set<uint64_t> groupIds;

    DateTime now = DateTime::Now();
    uint64_t oldestTimestampSeconds = now.Millis() / 1000ULL;
    uint64_t newestTimestampSeconds = 0ULL;

    for (const Metric &metric : aggregateDataPoints)
    {
        if (metric.Name != transferMetricName)
        {
            continue;
        }

        uint64_t timestampSeconds = metric.Timestamp / 1000ULL;
        oldestTimestampSeconds = std::min(oldestTimestampSeconds, timestampSeconds);
        newestTimestampSeconds = std::max(newestTimestampSeconds, timestampSeconds);
        groupIds.insert(metric.TransferId);
    }

    WritePerStreamCSVRowHeader(csvContents, oldestTimestampSeconds, newestTimestampSeconds);

    Vector<double> streamRow;
    Vector<double> overallRow;

    for (uint32_t i = 0; i < (uint32_t)PerStreamCSVColumn::ThroughputStart; ++i)
    {
        streamRow.push_back(0.0);
        overallRow.push_back(0.0);
    }

    for (uint64_t timestampSeconds = oldestTimestampSeconds; timestampSeconds <= newestTimestampSeconds;
         ++timestampSeconds)
    {
        streamRow.push_back(0.0);
        overallRow.push_back(0.0);
    }

    for (uint64_t groupId : groupIds)
    {
        GeneratePerStreamCSVRow(
            groupId,
            oldestTimestampSeconds,
            newestTimestampSeconds,
            transferMetricName,
            aggregateDataPointsLU,
            aggregateDataPoints,
            streamRow,
            overallRow);

        WritePerStreamCSVRow(csvContents, streamRow);
    }

    WritePerStreamCSVRow(csvContents, overallRow);

    return csvContents;
}

std::shared_ptr<StringStream> MetricsPublisher::GenerateMetricsBackupJson()
{
    std::lock_guard<std::mutex> lock(m_dataPointsLock);

    std::shared_ptr<StringStream> backupContents = MakeShared<StringStream>(g_allocator);
    String tabs;

    String transferTypeString = MetricTransferTypeToString(GetTransferType());
    String platformName = GetPlatformName();
    String toolName = GetToolName();
    String instanceType = GetInstanceType();
    bool encrypted = IsSendingEncrypted();

    *backupContents << tabs << "{" << std::endl;

    tabs.push_back('\t');

    *backupContents << tabs << "\"TransferType\": \"" << transferTypeString << "\"," << std::endl;
    *backupContents << tabs << "\"PlatformName\": \"" << platformName << "\"," << std::endl;
    *backupContents << tabs << "\"ToolName\": \"" << toolName << "\"," << std::endl;
    *backupContents << tabs << "\"InstanceType\": \"" << instanceType << "\"," << std::endl;
    *backupContents << tabs << "\"Encrypted\": " << encrypted << "," << std::endl;
    *backupContents << tabs << "\"Metrics\": [" << std::endl;

    tabs.push_back('\t');

    for (const Aws::Crt::Vector<Metric> &dataPoints : m_dataPointsPublished)
    {
        for (size_t i = 0; i < dataPoints.size(); ++i)
        {
            const Metric &metric = dataPoints[i];

            *backupContents << tabs << "{" << std::endl;

            tabs.push_back('\t');

            *backupContents << tabs << " \"Name\" : \"" << MetricNameToStr(metric.Name) << "\", " << std::endl;
            *backupContents << tabs << " \"Timestamp\" : \"" << metric.Timestamp << "\", " << std::endl;
            *backupContents << tabs << " \"Value\" : " << std::fixed << metric.Value << ", " << std::endl;
            *backupContents << tabs << " \"Unit\" : \"" << UnitToStr(metric.Unit) << "\" " << std::endl;

            tabs.pop_back();

            *backupContents << tabs << "}";

            if (i < dataPoints.size() - 1)
            {
                *backupContents << ",";
            }

            *backupContents << std::endl;
        }
    }

    tabs.pop_back();

    *backupContents << tabs << "]" << std::endl;

    tabs.pop_back();

    *backupContents << tabs << "}" << std::endl;

    return backupContents;
}

String MetricsPublisher::UploadBackup(uint32_t options)
{
    if (m_canaryApp.GetOptions().forkModeEnabled)
    {
        AWS_LOGF_WARN(AWS_LS_CRT_CPP_CANARY, "Metric backups not supported in fork mode.");
        return String();
    }

    String s3Path;

    {
        StringStream s3PathStream;

        uint64_t currentTicks = 0;
        aws_sys_clock_get_ticks(&currentTicks);
        uint64_t timestampNow = aws_timestamp_convert(currentTicks, AWS_TIMESTAMP_NANOS, AWS_TIMESTAMP_MILLIS, NULL);

        uint8_t dateBuffer[AWS_DATE_TIME_STR_MAX_LEN];
        AWS_ZERO_ARRAY(dateBuffer);
        auto dateBuf = ByteBufFromEmptyArray(dateBuffer, AWS_ARRAY_SIZE(dateBuffer));
        DateTime metricDateTime(timestampNow);
        metricDateTime.ToGmtString(DateFormat::ISO_8601, dateBuf);
        String dateStr((char *)dateBuf.buffer, dateBuf.len);

        for (size_t i = 0; i < dateStr.length(); ++i)
        {
            if (dateStr[i] == ':')
            {
                dateStr[i] = '-';
            }
        }

        s3PathStream << GetToolName() << "/" << GetPlatformName() << "/" << GetInstanceType() << "/" << dateStr << "/"
                     << CreateUUID() << "/";
        s3Path = s3PathStream.str();
    }

    std::mutex signalMutex;
    std::condition_variable signal;
    std::atomic<uint32_t> numFilesUploaded(0);

    std::shared_ptr<S3ObjectTransport> transport =
        MakeShared<S3ObjectTransport>(g_allocator, m_canaryApp, S3BackupBucket);

    String backupPath = s3Path + "metricsBackup.json";

    {
        AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Uploading metrics backup.");

        std::shared_ptr<StringStream> metricsBackupContents = GenerateMetricsBackupJson();
        std::shared_ptr<Io::StdIOStreamInputStream> metricsBackupContentsStream =
            MakeShared<Io::StdIOStreamInputStream>(g_allocator, metricsBackupContents);

        transport->PutObject(
            nullptr,
            backupPath,
            metricsBackupContentsStream,
            0,
            [&signal, &numFilesUploaded](int32_t, std::shared_ptr<Aws::Crt::String>) {
                ++numFilesUploaded;
                signal.notify_one();
            });
    }

    Map<AggregateMetricKey, size_t> aggregateDataPointsLU;
    Vector<Metric> aggregateDataPoints;

    {
        std::lock_guard<std::mutex> lock(m_dataPointsLock);

        for (const Vector<Metric> &dataPoints : m_dataPointsPublished)
        {
            AggregateDataPoints(dataPoints, aggregateDataPointsLU, aggregateDataPoints, true);
        }
    }

    {
        AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Uploading per stream upload metrics.");

        std::shared_ptr<StringStream> uploadCSVContents =
            GeneratePerStreamCSV(MetricName::BytesUp, aggregateDataPointsLU, aggregateDataPoints);

        std::shared_ptr<Io::StdIOStreamInputStream> uploadCSVContentsStream =
            MakeShared<Io::StdIOStreamInputStream>(g_allocator, uploadCSVContents);

        transport->PutObject(
            nullptr,
            s3Path + "uploadStreams.csv",
            uploadCSVContentsStream,
            0,
            [&signal, &numFilesUploaded](int32_t, std::shared_ptr<Aws::Crt::String>) {
                ++numFilesUploaded;
                signal.notify_one();
            });
    }

    {
        AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Uploading per stream download metrics.");

        std::shared_ptr<StringStream> downloadCSVContents =
            GeneratePerStreamCSV(MetricName::BytesDown, aggregateDataPointsLU, aggregateDataPoints);

        std::shared_ptr<Io::StdIOStreamInputStream> downloadCSVContentsStream =
            MakeShared<Io::StdIOStreamInputStream>(g_allocator, downloadCSVContents);

        transport->PutObject(
            nullptr,
            s3Path + "downloadStreams.csv",
            downloadCSVContentsStream,
            0,
            [&signal, &numFilesUploaded](int32_t, std::shared_ptr<Aws::Crt::String>) {
                ++numFilesUploaded;
                signal.notify_one();
            });
    }

    std::unique_lock<std::mutex> signalLock(signalMutex);
    signal.wait(signalLock, [&numFilesUploaded]() { return numFilesUploaded.load() >= 3; });

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Uploading backup finished.");

    if ((options & (uint32_t)UploadBackupOptions::PrintPath) != 0)
    {
        std::cout << "Path of back up is: " << backupPath << std::endl;
    }

    return backupPath;
}

void MetricsPublisher::RehydrateBackup(const char *s3Path)
{
    std::shared_ptr<S3ObjectTransport> transport =
        MakeShared<S3ObjectTransport>(g_allocator, m_canaryApp, S3BackupBucket);
    StringStream contents;
    std::mutex signalMutex;
    std::condition_variable signal;
    bool signalVal = false;

    transport->GetObject(
        nullptr,
        s3Path,
        0,
        [transport, &contents](const Http::HttpStream &, const ByteCursor &cur) { contents << cur.ptr; },
        [transport, &signalMutex, &signal, &signalVal](int32_t errorCode) {
            if (errorCode != AWS_ERROR_SUCCESS)
            {
                AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "Failed to rehydrate file: file download returned error.");
                return;
            }

            {
                std::lock_guard<std::mutex> locker(signalMutex);
                signalVal = true;
            }

            signal.notify_one();
        });

    std::unique_lock<std::mutex> lock(signalMutex);
    signal.wait(lock, [&signalVal]() { return signalVal; });

    String contentsStr = contents.str();
    JsonObject jsonObject(contentsStr);
    JsonView jsonView = jsonObject.View();

    String transferTypeStr = jsonView.GetString("TransferType");

    m_transferTypeOverride = StringToMetricTransferType(transferTypeStr.c_str());
    m_platformNameOverride = jsonView.GetString("PlatformName");
    m_toolNameOverride = jsonView.GetString("ToolName");
    m_instanceTypeOverride = jsonView.GetString("InstanceType");
    m_sendEncryptedOverride = jsonView.GetBool("Encrypted");

    m_replayId = CreateUUID();

    Vector<JsonView> metricsJson = jsonView.GetArray("Metrics");
    Vector<Metric> metrics;

    uint64_t newestTimeStamp = 0;

    for (const JsonView &metricJson : metricsJson)
    {
        String metricNameStr = metricJson.GetString("Name");
        String metricUnitStr = metricJson.GetString("Unit");

        String metricTimestampStr = metricJson.GetString("Timestamp");
        uint64_t metricTimestamp = std::stoull(metricTimestampStr.c_str());

        metrics.emplace_back(
            StringToMetricName(metricNameStr.c_str()),
            StringToMetricUnit(metricUnitStr.c_str()),
            metricTimestamp,
            metricJson.GetDouble("Value"));

        newestTimeStamp = std::max(metricTimestamp, newestTimeStamp);
    }

    /*
     * Calculate new timestamps for the metrics so that they happen within the most recent
     * three hour period, which will allow them to be graphed at a one second granularity
     * in CloudWatch.
     *
     * Because metric timestamps are not exactly on a one second boundary, some care must be
     * taken so that their given offset within a second is preserved.  Otherwise, a
     * metric might be incorrectly distributed to a neighboring second.
     */
    {
        /*
         * Calculate the amount of time, in milliseconds, that the newestTimeStamp
         * is from the next second.
         */
        uint64_t newestTimeStampSecondOffset = 1000ULL - (newestTimeStamp % 1000ULL);

        DateTime dateTime = DateTime::Now();
        uint64_t nowMillis = dateTime.Millis();

        /*
         * Given the time for "now" in milliseconds, back it up to the nearest second,
         * and then back it up by the newestTimeStampSecondOffset.
         */
        uint64_t relocatedMetricsEnd = nowMillis - (nowMillis % 1000ULL) - newestTimeStampSecondOffset;

        for (Metric &metric : metrics)
        {
            metric.Timestamp = metric.Timestamp - newestTimeStamp + relocatedMetricsEnd;
        }
    }

    AddDataPoints(metrics);
    SchedulePublish();
    WaitForLastPublish();

    std::stringstream cloudWatchMetricsLink;

    cloudWatchMetricsLink
        << "https://" << m_canaryApp.GetOptions().region << ".console.aws.amazon.com/cloudwatch/"
        << "home?region=" << m_canaryApp.GetOptions().region
        << "#metricsV2:graph=~(metrics~(~(~(expression~'m1*2a8*2f1000*2f1000*2f1000~label~'BytesDownGb~id~'e1))~(~("
           "expression~'m2*2a8*2f1000*2f1000*2f1000~label~'BytesUpGb~id~'e2))~(~'CRT-CPP-Canary-V2~'BytesDown"
        << "~'Platform~'" << m_platformNameOverride.value().c_str() << "~'ToolName~'"
        << m_toolNameOverride.value().c_str() << "~'Encrypted~'" << m_sendEncryptedOverride.value() << "~'ReplayId~'"
        << m_replayId.value() << "~'InstanceType~'" << m_instanceTypeOverride.value().c_str() << "~'TransferType~'"
        << transferTypeStr.c_str()
        << "~(id~'m1~visible~false))~(~'.~'BytesUp~'.~'.~'.~'.~'.~'.~'.~'.~'.~'.~'.~'.~(id~'m2~visible~false))~(~'.~'"
           "NumConnections~'.~'.~'.~'.~'.~'.~'.~'.~'.~'.~'.~'.~(id~'m3~visible~false))~(~'.~'FailedTransfer~'.~'.~'.~'."
           "~'.~'.~'.~'.~'.~'.~'.~'.~(id~'m4~visible~false))~(~'.~'SuccessfulTransfer~'.~'.~'.~'.~'.~'.~'.~'.~'.~'.~'.~"
           "'.~(id~'m5~visible~false))~(~'.~'S3AddressCount~'.~'.~'.~'.~'.~'.~'.~'.~'.~'.~'.~'.~(id~'m6~stat~'Average~"
           "visible~false)))~view~'timeSeries~stacked~false~region~'"
        << m_canaryApp.GetOptions().region
        << "~stat~'Sum~period~1~title~'Replay*20Graph)"
           ";query=~'*7bCRT-CPP-Canary-V2*2cEncrypted*2cInstanceType*2cPlatform*2cReplayId*2cToolName*2cTransferType*"
           "7d";

    std::cout << cloudWatchMetricsLink.str() << std::endl;

    m_transferTypeOverride = Optional<MetricTransferType>();
    m_platformNameOverride = Optional<String>();
    m_toolNameOverride = Optional<String>();
    m_instanceTypeOverride = Optional<String>();
    m_sendEncryptedOverride = Optional<bool>();
    m_replayId = Optional<String>();
}

void MetricsPublisher::SetTransferState(uint64_t transferId, const std::shared_ptr<TransferState> &transferState)
{
    std::lock_guard<std::mutex> lock(m_transferIdToStateLock);

    auto it = m_transferIdToState.find(transferId);

    if (it == m_transferIdToState.end())
    {
        m_transferIdToState.insert(std::pair<uint64_t, std::shared_ptr<TransferState>>(transferId, transferState));
    }
}

void MetricsPublisher::AddDataPoint(const Metric &newMetric)
{
    std::lock_guard<std::mutex> lock(m_dataPointsLock);

    m_dataPointsPending.push_back(newMetric);
}

void MetricsPublisher::AddDataPoints(const Vector<Metric> &newMetrics)
{
    std::lock_guard<std::mutex> lock(m_dataPointsLock);

    for (const Metric &newMetric : newMetrics)
    {
        m_dataPointsPending.push_back(newMetric);
    }
}

void MetricsPublisher::WaitForLastPublish()
{
    std::unique_lock<std::mutex> locker(m_publishDataLock);

    m_waitForLastPublishCV.wait(locker, [this]() { return m_publishData.size() == 0; });
}

void MetricsPublisher::s_OnPublishTask(aws_task *task, void *arg, aws_task_status status)
{
    (void)task;

    if (status != AWS_TASK_STATUS_RUN_READY)
    {
        return;
    }

    auto publisher = static_cast<MetricsPublisher *>(arg);

    if (publisher->m_publishDataTaskCopy.size() == 0)
    {
        std::lock_guard<std::mutex> locker(publisher->m_publishDataLock);

        // If there's no data left, schedule the next publish and send a notify that we've published everything we have.
        if (publisher->m_publishData.empty())
        {
            // publisher->SchedulePublish();
            publisher->m_waitForLastPublishCV.notify_all();
            return;
        }

        // Create a copy of the metrics to publish from
        {
            publisher->m_publishDataTaskCopy = std::move(publisher->m_publishData);
            publisher->m_publishData = Vector<Metric>();
        }
    }

    /* max of 20 per request */
    Vector<Metric> metricsSlice;
    while (!publisher->m_publishDataTaskCopy.empty() && metricsSlice.size() < 20)
    {
        metricsSlice.push_back(publisher->m_publishDataTaskCopy.back());
        publisher->m_publishDataTaskCopy.pop_back();
    }

    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY,
        "Processing %d metrics, %d left.",
        (uint32_t)metricsSlice.size(),
        (uint32_t)publisher->m_publishDataTaskCopy.size());

    auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
    request->AddHeader(publisher->m_hostHeader);
    request->AddHeader(publisher->m_contentTypeHeader);
    request->AddHeader(publisher->m_apiVersionHeader);

    auto bodyStream = MakeShared<StringStream>(g_allocator);
    publisher->PreparePayload(*bodyStream, metricsSlice);

    Http::HttpHeader contentLength;
    contentLength.name = ByteCursorFromCString("content-length");

    StringStream intValue;
    intValue << bodyStream->tellp();
    String contentLengthVal = intValue.str();
    contentLength.value = ByteCursorFromCString(contentLengthVal.c_str());
    request->AddHeader(contentLength);

    request->SetBody(bodyStream);
    request->SetMethod(aws_http_method_post);

    ByteCursor path = ByteCursorFromCString("/");
    request->SetPath(path);

    Auth::AwsSigningConfig signingConfig(g_allocator);
    signingConfig.SetRegion(publisher->m_canaryApp.GetOptions().region.c_str());
    signingConfig.SetCredentialsProvider(publisher->m_canaryApp.GetCredsProvider());
    signingConfig.SetService("monitoring");
    signingConfig.SetBodySigningType(Auth::BodySigningType::SignBody);
    signingConfig.SetSigningTimepoint(DateTime::Now());
    signingConfig.SetSigningAlgorithm(Auth::SigningAlgorithm::SigV4Header);

    publisher->m_canaryApp.GetSigner()->SignRequest(
        request,
        signingConfig,
        [bodyStream, publisher](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &signedRequest, int signingError) {
            if (signingError == AWS_OP_SUCCESS)
            {
                publisher->m_connManager->AcquireConnection(
                    [publisher, signedRequest](std::shared_ptr<Http::HttpClientConnection> conn, int connError) {
                        if (connError == AWS_OP_SUCCESS)
                        {
                            Http::HttpRequestOptions requestOptions;
                            requestOptions.request = signedRequest.get();
                            requestOptions.onStreamComplete = [signedRequest, conn](Http::HttpStream &stream, int) {
                                if (stream.GetResponseStatusCode() != 200)
                                {
                                    AWS_LOGF_ERROR(
                                        AWS_LS_CRT_CPP_CANARY,
                                        "Error in metrics stream complete: %d",
                                        stream.GetResponseStatusCode());
                                }
                            };
                            std::shared_ptr<Http::HttpClientStream> clientStream =
                                conn->NewClientStream(requestOptions);

                            if (clientStream == nullptr)
                            {
                                AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "Error creating stream to publish metrics.");
                            }
                            else
                            {
                                clientStream->Activate();
                            }
                        }
                        else
                        {
                            AWS_LOGF_ERROR(
                                AWS_LS_CRT_CPP_CANARY, "Error acquiring connection to send metrics: %d", connError);
                        }

                        publisher->SchedulePublish();
                    });
            }
            else
            {
                AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "Error signing request for sending metric: %d", signingError);
            }
        });
}
