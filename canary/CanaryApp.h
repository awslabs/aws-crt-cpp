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

#include <aws/common/logging.h>
#include <aws/common/system_info.h>
#include <aws/crt/Api.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/auth/Sigv4Signing.h>

#include <mutex>

class MetricsPublisher;
class S3ObjectTransport;
class MeasureTransferRate;

/*
 * Represents the options that define how the canary should run.  Most are values
 * defined by the command line.
 */
struct CanaryAppOptions
{
    CanaryAppOptions(const Aws::Crt::String &configFileName, const Aws::Crt::String &argv0) noexcept;

    Aws::Crt::String platformName;
    Aws::Crt::String toolName;
    Aws::Crt::String instanceType;
    Aws::Crt::String region;
    Aws::Crt::String httpTestEndpoint;
    Aws::Crt::String rehydrateBackupObjectName;
    Aws::Crt::String bucketName;
    Aws::Crt::String downloadObjectName;

    uint32_t numUpTransfers;
    uint32_t numUpConcurrentTransfers;
    uint32_t numDownTransfers;
    uint32_t numDownConcurrentTransfers;
    uint32_t numTransfersPerAddress;
    uint32_t maxNumThreads;

    uint64_t fileNameSuffixOffset;
    uint64_t singlePartObjectSize;
    uint64_t multiPartObjectPartSize;
    uint32_t multiPartObjectNumParts;
    uint32_t connectionMonitoringFailureIntervalSeconds;

    double targetThroughputGbps;

    uint32_t measureSinglePartTransfer : 1;
    uint32_t measureMultiPartTransfer : 1;
    uint32_t measureHttpTransfer : 1;
    uint32_t sendEncrypted : 1;
    uint32_t loggingEnabled : 1;
    uint32_t rehydrateBackup : 1;
    uint32_t connectionMonitoringEnabled : 1;
    uint32_t endPointMonitoringEnabled : 1;
    uint32_t metricsPublishingEnabled : 1;

    uint64_t GetMultiPartObjectSize() const { return multiPartObjectPartSize * (uint64_t)multiPartObjectNumParts; }
};

/*
 * Represents the running instance of the canary, and holds onto a lot of state
 * shared inbetween objects.
 */
class CanaryApp
{
  public:
    static void IncResourceRefCount();
    static void DecResourceRefCount();
    static void WaitForZeroResourceRefCount();

    static void ShutdownCallbackDecRefCount(void *user_data);

    CanaryApp(Aws::Crt::ApiHandle &apiHandle, CanaryAppOptions &&options) noexcept;

    void Run();

    const CanaryAppOptions &GetOptions() { return m_options; }
    Aws::Crt::Io::EventLoopGroup &GetEventLoopGroup() { return m_eventLoopGroup; }
    Aws::Crt::Io::DefaultHostResolver &GetDefaultHostResolver() { return m_defaultHostResolver; }
    Aws::Crt::Io::ClientBootstrap &GetBootstrap() { return m_bootstrap; }
    Aws::Crt::Io::TlsContext &GetTlsContext() { return m_tlsContext; }

    const std::shared_ptr<MetricsPublisher> &GetMetricsPublisher() const { return m_publisher; }
    const std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> &GetCredsProvider() const { return m_credsProvider; }
    const std::shared_ptr<Aws::Crt::Auth::Sigv4HttpRequestSigner> &GetSigner() const { return m_signer; }
    const std::shared_ptr<S3ObjectTransport> &GetTransport() const { return m_uploadTransport; }
    const std::shared_ptr<MeasureTransferRate> &GetMeasureTransferRate() const { return m_measureTransferRate; }

  private:
    static uint32_t s_resourceRefCount;
    static std::mutex s_resourceRefCountMutex;
    static std::condition_variable s_resourceRefCountSignal;

    Aws::Crt::ApiHandle &m_apiHandle;
    CanaryAppOptions m_options;

    Aws::Crt::Io::EventLoopGroup m_eventLoopGroup;
    Aws::Crt::Io::DefaultHostResolver m_defaultHostResolver;
    Aws::Crt::Io::ClientBootstrap m_bootstrap;
    Aws::Crt::Io::TlsContext m_tlsContext;

    std::shared_ptr<MetricsPublisher> m_publisher;
    std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> m_credsProvider;
    std::shared_ptr<Aws::Crt::Auth::Sigv4HttpRequestSigner> m_signer;
    std::shared_ptr<S3ObjectTransport> m_uploadTransport;
    std::shared_ptr<S3ObjectTransport> m_downloadTransport;
    std::shared_ptr<MeasureTransferRate> m_measureTransferRate;
};
