#pragma once

#include <aws/common/logging.h>
#include <aws/common/system_info.h>
#include <aws/crt/Api.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/auth/Sigv4Signing.h>

#include "CustomHostResolver.h"

class MetricsPublisher;
class S3ObjectTransport;
class MeasureTransferRate;

struct CanaryApp
{
    CanaryApp(int argc, char *argv[]);

    Aws::Crt::Allocator *traceAllocator;
    Aws::Crt::ApiHandle apiHandle;
    Aws::Crt::Io::EventLoopGroup eventLoopGroup;
    struct aws_host_resolution_config hostResolutionConfig;
    Aws::Crt::Io::DefaultHostResolver hostResolver;
    Aws::Crt::Io::ClientBootstrap bootstrap;
    CustomHostResolver customResolver;
    Aws::Crt::Io::TlsContext tlsContext;
    std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> credsProvider;
    std::shared_ptr<Aws::Crt::Auth::Sigv4HttpRequestSigner> signer;
    Aws::Crt::String platformName;
    Aws::Crt::String toolName;
    Aws::Crt::String instanceType;
    Aws::Crt::String region;
    double cutOffTimeSmallObjects;
    double cutOffTimeLargeObjects;
    bool measureLargeTransfer;
    bool measureSmallTransfer;
    bool usingNumaControl;
    bool sendEncrypted;

    std::shared_ptr<MetricsPublisher> publisher;
    std::shared_ptr<S3ObjectTransport> transport;
    std::shared_ptr<MeasureTransferRate> measureTransferRate;

    uint32_t seedCount;
};
