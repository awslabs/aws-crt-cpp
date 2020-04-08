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

struct CanaryAppOptions
{
    CanaryAppOptions() noexcept;

    // TODO these are currently std::strings due to fork mode needing
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
    std::string downloadBucketName;
    std::string downloadObjectName;

    int32_t readFromParentPipe;
    int32_t writeToParentPipe;
    uint32_t numUpTransfers;
    uint32_t numUpConcurrentTransfers;
    uint32_t numDownTransfers;
    uint32_t numDownConcurrentTransfers;
    uint32_t childProcessIndex;

    uint32_t measureSinglePartTransfer : 1;
    uint32_t measureMultiPartTransfer : 1;
    uint32_t measureHttpTransfer : 1;
    uint32_t usingNumaControl : 1;
    uint32_t downloadOnly : 1;
    uint32_t sendEncrypted : 1;
    uint32_t loggingEnabled : 1;
    uint32_t rehydrateBackup : 1;
    uint32_t isParentProcess : 1;
    uint32_t isChildProcess : 1;
};

struct CanaryAppChildProcess
{
    CanaryAppChildProcess() noexcept;
    CanaryAppChildProcess(pid_t pid, int32_t readPipe, int32_t writePipe) noexcept;

    pid_t pid;
    int32_t readFromChildPipe;
    int32_t writeToChildPipe;

    std::map<Aws::Crt::String, Aws::Crt::String> valuesFromChild;
};

class CanaryApp
{
  public:
    CanaryApp(CanaryAppOptions &&options, std::vector<CanaryAppChildProcess> &&children) noexcept;

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

    void WriteToChildProcess(uint32_t index, const char *key, const char *value);
    void WriteToParentProcess(const char *key, const char *value);

    Aws::Crt::String ReadFromChildProcess(uint32_t i, const char *key);
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
