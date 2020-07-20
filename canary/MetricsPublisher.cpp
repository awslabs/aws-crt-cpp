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
#include "MetricsPublisher.h"
#include "CanaryApp.h"
#include "MeasureTransferRate.h"

#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/http/HttpRequestResponse.h>

#include <aws/common/clock.h>
#include <aws/common/task_scheduler.h>
#include <condition_variable>
#include <inttypes.h>
#include <iostream>
#include <time.h>

using namespace Aws::Crt;

Metric::Metric() {}

Metric::Metric(const char *metricName, MetricUnit unit, double value) : Unit(unit), Value(value), MetricName(metricName)
{
    SetTimestampNow();
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
    Namespace = metricNamespace;

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

    auto connOptions = canaryApp.tlsContext.NewConnectionOptions();
    connOptions.SetServerName(serverName);
    connectionManagerOptions.ConnectionOptions.TlsOptions = connOptions;
    connectionManagerOptions.ConnectionOptions.Bootstrap = &canaryApp.bootstrap;
    connectionManagerOptions.MaxConnections = 5;

    m_connManager =
        Http::HttpClientConnectionManager::NewClientConnectionManager(connectionManagerOptions, g_allocator);

    m_schedulingLoop = aws_event_loop_group_get_next_loop(canaryApp.eventLoopGroup.GetUnderlyingHandle());
    uint64_t now = 0;
    aws_event_loop_current_clock_time(m_schedulingLoop, &now);
    aws_event_loop_schedule_task_future(m_schedulingLoop, &m_publishTask, now + m_publishFrequencyNs);

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

void MetricsPublisher::SetMetricTransferSize(MetricTransferSize transferSize)
{
    m_transferSize = transferSize;
}

static const char *s_UnitToStr(MetricUnit unit)
{
    static const char *s_unitStr[] = {
        "Seconds",
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
        "None",
    };

    auto index = static_cast<size_t>(unit);
    if (index >= AWS_ARRAY_SIZE(s_unitStr))
    {
        return "None";
    }
    return s_unitStr[index];
}

static const char *s_MetricTransferToString(MetricTransferSize transferSize)
{
    static const char *s_transferSizeStr[] = {"None", "Small", "Large"};

    auto index = static_cast<size_t>(transferSize);
    if (index >= AWS_ARRAY_SIZE(s_transferSizeStr))
    {
        return "None";
    }
    return s_transferSizeStr[index];
}

void MetricsPublisher::PreparePayload(Aws::Crt::StringStream &bodyStream, const Vector<Metric> &metrics)
{
    bodyStream << "Action=PutMetricData&";

    if (Namespace)
    {
        bodyStream << "Namespace=" << *Namespace << "&";
    }

    const CanaryAppOptions &options = m_canaryApp.GetOptions();

    size_t metricCount = 1;
    const char *transferSizeString = s_MetricTransferToString(m_transferSize);
    const char *platformName = options.platformName.c_str();
    const char *toolName = options.toolName.c_str();
    const char *instanceType = options.instanceType.c_str();
    const uint64_t largeObjectPartSize =
        MeasureTransferRate::LargeObjectSize / MeasureTransferRate::LargeObjectNumParts;

    for (const auto &metric : metrics)
    {
        bodyStream << "MetricData.member." << metricCount << ".MetricName=" << metric.MetricName << "&";
        uint8_t dateBuffer[AWS_DATE_TIME_STR_MAX_LEN];
        AWS_ZERO_ARRAY(dateBuffer);
        auto dateBuf = ByteBufFromEmptyArray(dateBuffer, AWS_ARRAY_SIZE(dateBuffer));
        DateTime metricDateTime(metric.Timestamp);
        metricDateTime.ToGmtString(DateFormat::ISO_8601, dateBuf);
        String dateStr((char *)dateBuf.buffer, dateBuf.len);

        bodyStream << "MetricData.member." << metricCount << ".Timestamp=" << dateStr << "&";
        bodyStream.precision(17);
        bodyStream << "MetricData.member." << metricCount << ".Value=" << std::fixed << metric.Value << "&";
        bodyStream << "MetricData.member." << metricCount << ".Unit=" << s_UnitToStr(metric.Unit) << "&";
        bodyStream << "MetricData.member." << metricCount << ".StorageResolution=1&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.1.Name=Platform&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.1.Value=" << platformName << "&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.2.Name=ToolName&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.2.Value=" << toolName << "&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.3.Name=InstanceType&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.3.Value=" << instanceType << "&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.4.Name=TransferSize&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.4.Value=" << transferSizeString << "&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.5.Name=UsingNumaControl&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.5.Value=" << options.usingNumaControl
                   << "&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.6.Name=Encrypted&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.6.Value=" << options.sendEncrypted
                   << "&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.7.Name=MTU&";
        bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.7.Value=" << options.mtu << "&";

        if (m_transferSize == MetricTransferSize::Large)
        {
            bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.8.Name=NumParts&";
            bodyStream << "MetricData.member." << metricCount
                       << ".Dimensions.member.8.Value=" << MeasureTransferRate::LargeObjectNumParts << "&";
            bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.9.Name=PartSize&";
            bodyStream << "MetricData.member." << metricCount << ".Dimensions.member.9.Value=" << largeObjectPartSize
                       << "&";
        }

        metricCount++;
    }

    bodyStream << "Version=2010-08-01";
}

void MetricsPublisher::AddDataPoint(const Metric &newMetric)
{
    std::lock_guard<std::mutex> locker(m_publishDataLock);

    AddDataPointInternal(newMetric);
}

void MetricsPublisher::AddDataPoints(const Vector<Metric> &newMetrics)
{
    std::lock_guard<std::mutex> locker(m_publishDataLock);

    uint32_t numBefore = (uint32_t)m_publishData.size();

    for (const Metric &newMetric : newMetrics)
    {
        AddDataPointInternal(newMetric);
    }

    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY, "Number of metrics before/after: %d/%d", numBefore, (uint32_t)m_publishData.size());
}

void MetricsPublisher::AddDataPointInternal(const Metric &newMetric)
{
    bool foundMatch = false;

    // TODO average size of m_publishData may justify doing a lookup here
    // instead of a linear search, especially in the case of AddDataPoints.
    for (Metric &existingMetric : m_publishData)
    {
        if (existingMetric.Timestamp == newMetric.Timestamp && existingMetric.MetricName == newMetric.MetricName)
        {
            existingMetric.Value += newMetric.Value;
            foundMatch = true;
            break;
        }
    }

    if (!foundMatch)
    {
        m_publishData.push_back(newMetric);
    }
}

void MetricsPublisher::AddTransferStatusDataPoint(bool transferSuccess)
{
    if (transferSuccess)
    {
        Metric successMetric;
        successMetric.MetricName = "SuccessfulTransfer";
        successMetric.Unit = MetricUnit::Count;
        successMetric.Value = 1;
        successMetric.SetTimestampNow();

        AddDataPoint(successMetric);
    }
    else
    {
        Metric failureMetric;
        failureMetric.MetricName = "FailedTransfer";
        failureMetric.Unit = MetricUnit::Count;
        failureMetric.Value = 1;
        failureMetric.SetTimestampNow();

        AddDataPoint(failureMetric);
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

    Vector<Metric> metricsCpy;
    {
        std::lock_guard<std::mutex> locker(publisher->m_publishDataLock);

        // If there's no data left, schedule the next publish and send a notify that we've published everything we have.
        if (publisher->m_publishData.empty())
        {
            uint64_t now = 0;
            aws_event_loop_current_clock_time(publisher->m_schedulingLoop, &now);
            aws_event_loop_schedule_task_future(
                publisher->m_schedulingLoop, &publisher->m_publishTask, now + publisher->m_publishFrequencyNs);
            publisher->m_waitForLastPublishCV.notify_all();
            return;
        }

        // Create a copy of the metrics to publish from
        {
            metricsCpy = std::move(publisher->m_publishData);
            publisher->m_publishData = Vector<Metric>();
        }
    }

    while (!metricsCpy.empty())
    {
        bool finalRun = false;
        /* max of 20 per request */
        Vector<Metric> metricsSlice;
        while (!metricsCpy.empty() && metricsSlice.size() < 20)
        {
            metricsSlice.push_back(metricsCpy.back());
            metricsCpy.pop_back();
        }

        AWS_LOGF_INFO(
            AWS_LS_CRT_CPP_CANARY,
            "METRICS - Processing %d metrics,  %d left.",
            (uint32_t)metricsSlice.size(),
            (uint32_t)metricsCpy.size());

        finalRun = metricsCpy.empty();

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
        signingConfig.SetCredentialsProvider(publisher->m_canaryApp.credsProvider);
        signingConfig.SetService("monitoring");
        signingConfig.SetBodySigningType(Auth::BodySigningType::SignBody);
        signingConfig.SetSigningTimepoint(DateTime::Now());
        signingConfig.SetSigningAlgorithm(Auth::SigningAlgorithm::SigV4Header);

        publisher->m_canaryApp.signer->SignRequest(
            request,
            signingConfig,
            [bodyStream, publisher, finalRun](
                const std::shared_ptr<Aws::Crt::Http::HttpRequest> &signedRequest, int signingError) {
                if (signingError == AWS_OP_SUCCESS)
                {
                    publisher->m_connManager->AcquireConnection(
                        [publisher, signedRequest, finalRun](
                            std::shared_ptr<Http::HttpClientConnection> conn, int connError) {
                            if (connError == AWS_OP_SUCCESS)
                            {
                                Http::HttpRequestOptions requestOptions;
                                AWS_ZERO_STRUCT(requestOptions);
                                requestOptions.request = signedRequest.get();
                                requestOptions.onStreamComplete = [signedRequest, conn](Http::HttpStream &stream, int) {
                                    if (stream.GetResponseStatusCode() != 200)
                                    {
                                        AWS_LOGF_ERROR(
                                            AWS_LS_CRT_CPP_CANARY,
                                            "METRICS Error in metrics stream complete: %d",
                                            stream.GetResponseStatusCode());
                                    }
                                };
                                conn->NewClientStream(requestOptions);
                            }
                            else
                            {
                                AWS_LOGF_ERROR(
                                    AWS_LS_CRT_CPP_CANARY,
                                    "METRICS Error acquiring connection to send metrics: %d",
                                    connError);
                            }

                            if (finalRun)
                            {
                                uint64_t now = 0;
                                aws_event_loop_current_clock_time(publisher->m_schedulingLoop, &now);
                                aws_event_loop_schedule_task_future(
                                    publisher->m_schedulingLoop,
                                    &publisher->m_publishTask,
                                    now + publisher->m_publishFrequencyNs);
                            }
                        });
                }
                else
                {
                    AWS_LOGF_ERROR(
                        AWS_LS_CRT_CPP_CANARY, "METRICS Error signing request for sending metric: %d", signingError);
                }
            });

        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}
