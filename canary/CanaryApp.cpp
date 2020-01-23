#include "CanaryApp.h"
#include "CanaryUtil.h"
#include "MeasureTransferRate.h"
#include "MetricsPublisher.h"
#include "S3ObjectTransport.h"
extern "C"
{
#include <aws/common/command_line_parser.h>
}

#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/auth/Credentials.h>

using namespace Aws::Crt;

CanaryApp::CanaryApp(int argc, char *argv[])
    : traceAllocator(aws_mem_tracer_new(DefaultAllocator(), NULL, AWS_MEMTRACE_BYTES, 0)), apiHandle(traceAllocator),
      eventLoopGroup(traceAllocator), defaultHostResolver(eventLoopGroup, 60, 1000, traceAllocator),
      bootstrap(eventLoopGroup, defaultHostResolver, traceAllocator), platformName(CanaryUtil::GetPlatformName()),
      toolName("NA"), instanceType("unknown"), region("us-west-2"), cutOffTimeSmallObjects(10.0),
      cutOffTimeLargeObjects(10.0), measureLargeTransfer(false), measureSmallTransfer(false)
{
    apiHandle.InitializeLogging(LogLevel::Info, stderr);
    /*
        Auth::CredentialsProviderChainDefaultConfig chainConfig;
        chainConfig.Bootstrap = &bootstrap;

        credsProvider = Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(chainConfig, traceAllocator);
    */

    Auth::CredentialsProviderImdsConfig imdsConfig;
    imdsConfig.Bootstrap = &bootstrap;

    credsProvider = Auth::CredentialsProvider::CreateCredentialsProviderImds(imdsConfig, traceAllocator);

    signer = MakeShared<Auth::Sigv4HttpRequestSigner>(traceAllocator, traceAllocator);

    Io::TlsContextOptions tlsContextOptions = Io::TlsContextOptions::InitDefaultClient(traceAllocator);
    tlsContext = Io::TlsContext(tlsContextOptions, Io::TlsMode::CLIENT, traceAllocator);

    enum class CLIOption
    {
        ToolName,
        InstanceType,
        CutOffTimeSmall,
        CutOffTimelarge,
        MeasureLargeTransfer,
        MeasureSmallTransfer,

        MAX
    };

    const aws_cli_option options[] = {{"toolName", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 't'},
                                      {"instanceType", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'i'},
                                      {"cutOffTimeSmall", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'c'},
                                      {"cutOffTimeLarge", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'C'},
                                      {"measureLargeTransfer", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'l'},
                                      {"measureSmallTransfer", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 's'}};

    const char *optstring = "t:i:c:C:ls";
    toolName = argc >= 1 ? argv[0] : "NA";

    size_t dirStart = toolName.rfind('\\');

    if (dirStart != String::npos)
    {
        toolName = toolName.substr(dirStart + 1);
    }

    int cliOptionIndex = 0;

    while (aws_cli_getopt_long(argc, argv, optstring, options, &cliOptionIndex) != -1)
    {
        switch ((CLIOption)cliOptionIndex)
        {
            case CLIOption::ToolName:
                toolName = aws_cli_optarg;
                break;
            case CLIOption::InstanceType:
                instanceType = aws_cli_optarg;
                break;
            case CLIOption::CutOffTimeSmall:
                cutOffTimeSmallObjects = atof(aws_cli_optarg);
                break;
            case CLIOption::CutOffTimelarge:
                cutOffTimeLargeObjects = atof(aws_cli_optarg);
                break;
            case CLIOption::MeasureLargeTransfer:
                measureLargeTransfer = true;
                break;
            case CLIOption::MeasureSmallTransfer:
                measureSmallTransfer = true;
                break;
            default:
                AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "Unknown CLI option used.");
                break;
        }
    }

    publisher = MakeShared<MetricsPublisher>(g_allocator, *this, "CRT-CPP-Canary");
    transport = MakeShared<S3ObjectTransport>(g_allocator, *this, "aws-crt-canary-bucket-rc");
    measureTransferRate = MakeShared<MeasureTransferRate>(g_allocator, *this);
}
