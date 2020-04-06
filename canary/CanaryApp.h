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
 * defined by the command line, with some exceptions for values passed from
 * a parent process to a child process in fork mode.
 */
struct CanaryAppOptions
{
    CanaryAppOptions() noexcept;

    // These are currently std::strings due to fork mode needing
    // to fork before the apiHandle is ready, otherwise systems may not
    // be set up correctly in that subprocess.  Trying to use Aws::Crt::Strings
    // before the ApiHandle is ready has awkward side effects with g_allocator
    // and can cause fatal asserts.
    std::string platformName;
    std::string toolName;
    std::string instanceType;
    std::string region;
    std::string httpTestEndpoint;
    std::string rehydrateBackupObjectName;
    std::string bucketName;
    std::string downloadObjectName;

    int32_t readFromParentPipe;
    int32_t writeToParentPipe;
    uint32_t numUpTransfers;
    uint32_t numUpConcurrentTransfers;
    uint32_t numDownTransfers;
    uint32_t numDownConcurrentTransfers;
    uint32_t childProcessIndex;
    uint32_t numTransfersPerAddress;

    uint64_t singlePartObjectSize;
    uint64_t multiPartObjectPartSize;
    uint32_t multiPartObjectNumParts;

    uint32_t measureSinglePartTransfer : 1;
    uint32_t measureMultiPartTransfer : 1;
    uint32_t measureHttpTransfer : 1;
    uint32_t usingNumaControl : 1;
    uint32_t downloadOnly : 1;
    uint32_t sendEncrypted : 1;
    uint32_t loggingEnabled : 1;
    uint32_t rehydrateBackup : 1;
    uint32_t forkModeEnabled : 1;
    uint32_t isParentProcess : 1;
    uint32_t isChildProcess : 1;

    uint64_t GetMultiPartObjectSize() const { return multiPartObjectPartSize * (uint64_t)multiPartObjectNumParts; }

    uint32_t GetNumUpConcurrentPartTransfers() const { return numUpConcurrentTransfers * multiPartObjectNumParts; }

    uint32_t GetNumDownConcurrentPartTransfers() const { return numDownConcurrentTransfers * multiPartObjectNumParts; }

    uint32_t GetMultiPartNumTransfersPerAddress() const { return numTransfersPerAddress * multiPartObjectNumParts; }
};

#ifndef WIN32
/*
 * Represents a child process in fork mode.
 */
struct CanaryAppChildProcess
{
    CanaryAppChildProcess() noexcept;
    CanaryAppChildProcess(pid_t pid, int32_t readPipe, int32_t writePipe) noexcept;
    pid_t pid;

    int32_t readFromChildPipe;
    int32_t writeToChildPipe;

    std::map<Aws::Crt::String, Aws::Crt::String> valuesFromChild;
};
#endif

/*
 * Represents the running instance of the canary, and holds onto a lot of state
 * shared inbetween objects.
 */
class CanaryApp
{
  public:
    CanaryApp(CanaryAppOptions &&options) noexcept;
#ifndef WIN32
    CanaryApp(CanaryAppOptions &&options, std::vector<CanaryAppChildProcess> &&children) noexcept;
#endif

    void Run();

    const CanaryAppOptions &GetOptions() { return m_options; }
    Aws::Crt::Io::EventLoopGroup &GetEventLoopGroup() { return m_eventLoopGroup; }
    Aws::Crt::Io::DefaultHostResolver &GetDefaultHostResolver() { return m_defaultHostResolver; }
    Aws::Crt::Io::ClientBootstrap &GetBootstrap() { return m_bootstrap; }
    Aws::Crt::Io::TlsContext &GetTlsContext() { return m_tlsContext; }

    const std::shared_ptr<MetricsPublisher> &GetMetricsPublisher() const { return m_publisher; }
    const std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> &GetCredsProvider() const { return m_credsProvider; }
    const std::shared_ptr<Aws::Crt::Auth::Sigv4HttpRequestSigner> &GetSigner() const { return m_signer; }
    const std::shared_ptr<S3ObjectTransport> &GetUploadTransport() const { return m_uploadTransport; }
    const std::shared_ptr<S3ObjectTransport> &GetDownloadTransport() const { return m_downloadTransport; }
    const std::shared_ptr<MeasureTransferRate> &GetMeasureTransferRate() const { return m_measureTransferRate; }

    /*
     * Write a key-value pair to a child process.
     */

    /*
     * Write a key-value pair to the parent process.
     */
    void WriteToChildProcess(uint32_t index, const char *key, const char *value);
    void WriteToParentProcess(const char *key, const char *value);

    /*
     * Read a key-value pair from a child process.
     */
    Aws::Crt::String ReadFromChildProcess(uint32_t i, const char *key);

    /*
     * Read a key-value pair from the parent process.
     */
    Aws::Crt::String ReadFromParentProcess(const char *key);

  private:
    CanaryAppOptions m_options;

    Aws::Crt::ApiHandle m_apiHandle;
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

#ifndef WIN32
    std::vector<CanaryAppChildProcess> children;
    std::map<Aws::Crt::String, Aws::Crt::String> valuesFromParent;

    void WriteKeyValueToPipe(const char *key, const char *value, uint32_t writePipe);

    Aws::Crt::String ReadValueFromPipe(
        const char *key,
        int32_t readPipe,
        std::map<Aws::Crt::String, Aws::Crt::String> &keyValuePairs);
    std::pair<Aws::Crt::String, Aws::Crt::String> ReadNextKeyValuePairFromPipe(int32_t readPipe);
#endif
};
