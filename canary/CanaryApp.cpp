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

#include "CanaryApp.h"
#include "CanaryUtil.h"
#include "MeasureTransferRate.h"
#include "MetricsPublisher.h"
#include "S3ObjectTransport.h"

#include <aws/crt/Api.h>
#include <aws/crt/JsonObject.h>
#include <aws/crt/Types.h>
#include <aws/crt/auth/Credentials.h>

#include <aws/common/log_channel.h>
#include <aws/common/log_formatter.h>
#include <aws/common/log_writer.h>

#include <fstream>

#ifndef WIN32
#    include <sys/resource.h>
#    include <sys/types.h>
#    include <sys/wait.h>
#    include <unistd.h>
#endif

using namespace Aws::Crt;

namespace
{
    const char *MetricNamespace = "CRT-CPP-Canary-V2";
} // namespace

CanaryAppOptions::CanaryAppOptions(const String &configFileName) noexcept
    : platformName(CanaryUtil::GetPlatformName().c_str()), toolName("S3Canary"), instanceType("unknown"),
      region("us-west-2"), bucketName("aws-crt-canary-bucket"), numUpTransfers(0), numUpConcurrentTransfers(0),
      numDownTransfers(0), numDownConcurrentTransfers(0), numTransfersPerAddress(10), fileNameSuffixOffset(1),
      singlePartObjectSize(5ULL * 1024ULL * 1024ULL * 1024ULL), multiPartObjectPartSize(52LL * 1024ULL * 1024ULL),
      multiPartObjectNumParts(100), connectionMonitoringFailureIntervalSeconds(1), targetThroughputGbps(80.0),
      measureSinglePartTransfer(false), measureMultiPartTransfer(false), measureHttpTransfer(false),
      sendEncrypted(false), loggingEnabled(false), rehydrateBackup(false), connectionMonitoringEnabled(false),
      endPointMonitoringEnabled(false), metricsPublishingEnabled(true)
{
    if (configFileName.empty())
    {
        return;
    }

    std::ifstream configFileStream(configFileName.c_str());

    if (!configFileStream.is_open())
    {
        return;
    }

#define GET_CONFIG_VALUE(view, jsonType, key, var)                                                                     \
    if ((view).KeyExists((key)))                                                                                       \
    {                                                                                                                  \
        var = (view).Get##jsonType((key));                                                                             \
    }

#define GET_CONFIG_VALUE_CAST(view, jsonType, castType, key, var)                                                      \
    if ((view).KeyExists((key)))                                                                                       \
    {                                                                                                                  \
        var = (castType)(view).Get##jsonType((key));                                                                   \
    }

    String fileContents((std::istreambuf_iterator<char>(configFileStream)), std::istreambuf_iterator<char>());

    JsonObject jsonContents(fileContents);
    JsonView jsonView = jsonContents.View();

    GET_CONFIG_VALUE(jsonView, String, "PlatformName", platformName);
    GET_CONFIG_VALUE(jsonView, String, "ToolName", toolName);
    GET_CONFIG_VALUE(jsonView, String, "InstanceType", instanceType);
    GET_CONFIG_VALUE(jsonView, String, "Region", region);
    GET_CONFIG_VALUE(jsonView, String, "HttpTestEndpoint", httpTestEndpoint);
    GET_CONFIG_VALUE(jsonView, String, "BucketName", bucketName);
    GET_CONFIG_VALUE(jsonView, String, "DownloadObjectName", downloadObjectName);

    GET_CONFIG_VALUE_CAST(jsonView, Integer, uint32_t, "NumUpTransfers", numUpTransfers);
    GET_CONFIG_VALUE_CAST(jsonView, Integer, uint32_t, "NumUpConcurrentTransfers", numUpConcurrentTransfers);
    GET_CONFIG_VALUE_CAST(jsonView, Integer, uint32_t, "NumDownTransfers", numDownTransfers);
    GET_CONFIG_VALUE_CAST(jsonView, Integer, uint32_t, "NumDownConcurrentTransfers", numDownConcurrentTransfers);
    GET_CONFIG_VALUE_CAST(jsonView, Integer, uint32_t, "NumTransfersPerAddress", numTransfersPerAddress);

    GET_CONFIG_VALUE_CAST(jsonView, Int64, uint64_t, "FileNameSuffixOffset", fileNameSuffixOffset);
    GET_CONFIG_VALUE_CAST(jsonView, Int64, uint64_t, "SinglePartObjectSize", singlePartObjectSize);
    GET_CONFIG_VALUE_CAST(jsonView, Int64, uint64_t, "MultiPartObjectPartSize", multiPartObjectPartSize);
    GET_CONFIG_VALUE_CAST(jsonView, Integer, uint32_t, "MultipartObjectNumParts", multiPartObjectNumParts);
    GET_CONFIG_VALUE_CAST(
        jsonView,
        Integer,
        uint32_t,
        "ConnectionMonitoringFailureIntervalSeconds",
        connectionMonitoringFailureIntervalSeconds);

    GET_CONFIG_VALUE(jsonView, Double, "TargetThroughputGbps", targetThroughputGbps);

    GET_CONFIG_VALUE(jsonView, Bool, "MeasureSinglePartTransfer", measureSinglePartTransfer);
    GET_CONFIG_VALUE(jsonView, Bool, "MeasureMultiPartTransfer", measureMultiPartTransfer);
    GET_CONFIG_VALUE(jsonView, Bool, "MeasureHttpTransfer", measureHttpTransfer);
    GET_CONFIG_VALUE(jsonView, Bool, "SendEncrypted", sendEncrypted);
    GET_CONFIG_VALUE(jsonView, Bool, "LoggingEnabled", loggingEnabled);
    GET_CONFIG_VALUE(jsonView, Bool, "ConnectionMonitoringEnabled", connectionMonitoringEnabled);
    GET_CONFIG_VALUE(jsonView, Bool, "EndPointMonitoringEnabled", endPointMonitoringEnabled);
    GET_CONFIG_VALUE(jsonView, Bool, "MetricsPublishingEnabled", metricsPublishingEnabled);

#undef GET_CONFIG_VALUE_CAST
#undef GET_CONFIG_VALUE
}

CanaryApp::CanaryApp(Aws::Crt::ApiHandle &apiHandle, CanaryAppOptions &&inOptions) noexcept
    : m_apiHandle(apiHandle), m_options(inOptions), m_eventLoopGroup(0, g_allocator),
      m_defaultHostResolver(m_eventLoopGroup, 60, 3600, g_allocator),
      m_bootstrap(m_eventLoopGroup, m_defaultHostResolver, g_allocator)
{
#ifndef WIN32
    // Default FDS limit on Linux can be quite low at at 1024, so
    // increase it for added head room.
    rlimit fdsLimit;
    getrlimit(RLIMIT_NOFILE, &fdsLimit);
    fdsLimit.rlim_cur = fdsLimit.rlim_max;
    setrlimit(RLIMIT_NOFILE, &fdsLimit);
#endif

    // Increase channel fragment size to 256k, due to the added
    // throughput increase.
    const size_t KB_256 = 256 * 1024;
    g_aws_channel_max_fragment_size = KB_256;

    if (m_options.loggingEnabled)
    {
        m_apiHandle.InitializeLogging(LogLevel::Info, stderr);
    }

    Auth::CredentialsProviderChainDefaultConfig chainConfig;
    chainConfig.Bootstrap = &m_bootstrap;

    m_credsProvider = Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(chainConfig, g_allocator);

    m_signer = MakeShared<Auth::Sigv4HttpRequestSigner>(g_allocator, g_allocator);

    Io::TlsContextOptions tlsContextOptions = Io::TlsContextOptions::InitDefaultClient(g_allocator);
    m_tlsContext = Io::TlsContext(tlsContextOptions, Io::TlsMode::CLIENT, g_allocator);

    uint64_t perConnThroughputUp = 0ULL;
    uint64_t perConnThroughputDown = 0ULL;

    double targetThroughputBytesPerSecond = m_options.targetThroughputGbps * 1000.0 * 1000.0 * 1000.0 / 8.0;

    if (m_options.measureMultiPartTransfer)
    {
        if (m_options.numUpConcurrentTransfers > 0)
        {
            perConnThroughputUp = targetThroughputBytesPerSecond / m_options.numUpConcurrentTransfers;
        }

        if (m_options.numDownConcurrentTransfers > 0)
        {
            perConnThroughputDown = targetThroughputBytesPerSecond / m_options.numDownConcurrentTransfers;
        }
    }

    m_publisher = MakeShared<MetricsPublisher>(g_allocator, *this, MetricNamespace);
    m_uploadTransport = MakeShared<S3ObjectTransport>(
        g_allocator, *this, m_options.bucketName.c_str(), m_options.numUpConcurrentTransfers, perConnThroughputUp);
    m_downloadTransport = MakeShared<S3ObjectTransport>(
        g_allocator, *this, m_options.bucketName.c_str(), m_options.numDownConcurrentTransfers, perConnThroughputDown);
    m_measureTransferRate = MakeShared<MeasureTransferRate>(g_allocator, *this);
}

void CanaryApp::Run()
{
    if (m_options.rehydrateBackup)
    {
        m_publisher->RehydrateBackup(m_options.rehydrateBackupObjectName.c_str());
    }

    if (m_options.measureSinglePartTransfer)
    {
        m_publisher->SetMetricTransferType(MetricTransferType::SinglePart);
        m_measureTransferRate->MeasureSinglePartObjectTransfer();
    }

    if (m_options.measureMultiPartTransfer)
    {
        m_publisher->SetMetricTransferType(MetricTransferType::MultiPart);
        m_measureTransferRate->MeasureMultiPartObjectTransfer();
    }

    if (m_options.measureHttpTransfer)
    {
        m_publisher->SetMetricTransferType(MetricTransferType::SinglePart);
        m_measureTransferRate->MeasureHttpTransfer();
    }
}
