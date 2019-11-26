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
#include <aws/crt/DateTime.h>
#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/auth/Sigv4Signing.h>

#include <chrono>
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

struct Metric
{
    MetricUnit Unit;
    Aws::Crt::DateTime Timestamp;
    double Value;
    Aws::Crt::String MetricName;
};

class MetricsPublisher
{
public:
    MetricsPublisher(const Aws::Crt::String region,
            Aws::Crt::Io::TlsContext &tlsContext,
            Aws::Crt::Io::ClientBootstrap &clientBootstrap,
            Aws::Crt::Io::EventLoopGroup &elGroup,
            const std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> &credsProvider,
            const std::shared_ptr<Aws::Crt::Auth::Sigv4HttpRequestSigner> &signer,
            std::chrono::seconds publishFrequency = std::chrono::seconds(1));
    void AddDataPoint(Metric&& metricData);

private:
    static void s_OnPublishTask(aws_task *task, void *arg, aws_task_status status);

    std::shared_ptr<Aws::Crt::Http::HttpClientConnectionManager> m_connManager;
    std::shared_ptr<Aws::Crt::Auth::Sigv4HttpRequestSigner> m_signer;
    std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> m_credsProvider;
    Aws::Crt::Io::EventLoopGroup& m_elGroup;
    Aws::Crt::Vector<Metric> m_publishData;
    std::mutex m_publishDataLock;
    aws_task m_publishTask;
    uint64_t m_publishFrequencyNs;
};
