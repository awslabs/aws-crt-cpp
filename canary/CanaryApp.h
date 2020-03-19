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

    int32_t readFromParentPipe;
    int32_t writeToParentPipe;
    uint32_t mtu;
    uint32_t numUpTransfers;
    uint32_t numUpConcurrentTransfers;
    uint32_t numDownTransfers;
    uint32_t numDownConcurrentTransfers;
    uint32_t childProcessIndex;

    uint32_t measureLargeTransfer : 1;
    uint32_t measureSmallTransfer : 1;
    uint32_t measureHttpTransfer : 1;
    uint32_t usingNumaControl : 1;
    uint32_t sendEncrypted : 1;
    uint32_t loggingEnabled : 1;
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

    Aws::Crt::Allocator *traceAllocator;
    Aws::Crt::ApiHandle apiHandle;
    Aws::Crt::Io::EventLoopGroup eventLoopGroup;
    Aws::Crt::Io::DefaultHostResolver defaultHostResolver;
    Aws::Crt::Io::ClientBootstrap bootstrap;
    Aws::Crt::Io::TlsContext tlsContext;

    std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> credsProvider;
    std::shared_ptr<Aws::Crt::Auth::Sigv4HttpRequestSigner> signer;
    std::shared_ptr<MetricsPublisher> publisher;
    std::shared_ptr<S3ObjectTransport> transport;
    std::shared_ptr<S3ObjectTransport> transportSecondary;
    std::shared_ptr<MeasureTransferRate> measureTransferRate;

    const CanaryAppOptions &GetOptions() { return options; }

    void WriteToChildProcess(uint32_t index, const char *key, const char *value);
    void WriteToParentProcess(const char *key, const char *value);

    Aws::Crt::String ReadFromChildProcess(uint32_t i, const char *key);
    Aws::Crt::String ReadFromParentProcess(const char *key);

  private:
    CanaryAppOptions options;

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
