#pragma once

#include <aws/common/logging.h>
#include <aws/common/system_info.h>
#include <aws/crt/Api.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/auth/Sigv4Signing.h>

class MetricsPublisher;
class S3ObjectTransport;
class MeasureTransferRate;

class CanaryApp
{
public:

    CanaryApp(int argc, char *argv[]);

    Aws::Crt::Allocator *traceAllocator;
    Aws::Crt::ApiHandle apiHandle;
    Aws::Crt::Io::EventLoopGroup eventLoopGroup;
    Aws::Crt::Io::DefaultHostResolver defaultHostResolver;
    Aws::Crt::Io::ClientBootstrap bootstrap;
    Aws::Crt::Io::TlsContext tlsContext;
    std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> credsProvider;
    std::shared_ptr<Aws::Crt::Auth::Sigv4HttpRequestSigner> signer;

    Aws::Crt::String platformName;
    Aws::Crt::String toolName;
    Aws::Crt::String instanceType;
    Aws::Crt::String region;

    uint32_t mtu;

    uint32_t measureLargeTransfer : 1;
    uint32_t measureSmallTransfer : 1;
    uint32_t usingNumaControl : 1;
    uint32_t sendEncrypted : 1;
    uint32_t forkProcesses : 1;

    std::shared_ptr<MetricsPublisher> publisher;
    std::shared_ptr<S3ObjectTransport> transport;
    std::shared_ptr<MeasureTransferRate> measureTransferRate;
};
