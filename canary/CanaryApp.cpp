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

#include <aws/common/log_channel.h>
#include <aws/common/log_formatter.h>
#include <aws/common/log_writer.h>

#ifdef __linux__
#    include <sys/resource.h>
#endif

using namespace Aws::Crt;

int filterLog(
    struct aws_logger *logger,
    enum aws_log_level log_level,
    aws_log_subject_t subject,
    const char *format,
    ...)
{
    if (log_level != AWS_LL_ERROR)
    {
        if (subject != AWS_LS_CRT_CPP_CANARY)
        {
            return AWS_OP_SUCCESS;
        }
    }

    va_list format_args;
    va_start(format_args, format);

    struct aws_logger_pipeline *impl = (aws_logger_pipeline *)logger->p_impl;
    struct aws_string *output = NULL;

    AWS_ASSERT(impl->formatter->vtable->format != NULL);
    int result = (impl->formatter->vtable->format)(impl->formatter, &output, log_level, subject, format, format_args);

    va_end(format_args);

    if (result != AWS_OP_SUCCESS || output == NULL)
    {
        return AWS_OP_ERR;
    }

    AWS_ASSERT(impl->channel->vtable->send != NULL);
    if ((impl->channel->vtable->send)(impl->channel, output))
    {
        /*
         * failure to send implies failure to transfer ownership
         */
        aws_string_destroy(output);
        return AWS_OP_ERR;
    }

    return AWS_OP_SUCCESS;
}

CanaryApp::CanaryApp(int argc, char *argv[])
    : traceAllocator(DefaultAllocator()), apiHandle(traceAllocator), eventLoopGroup(32, traceAllocator),
      defaultHostResolver(eventLoopGroup, 60, 1000, traceAllocator),
      bootstrap(eventLoopGroup, defaultHostResolver, traceAllocator), platformName(CanaryUtil::GetPlatformName()),
      toolName("NA"), instanceType("unknown"), region("us-west-2"), cutOffTimeSmallObjects(10.0),
      cutOffTimeLargeObjects(10.0), measureLargeTransfer(false), measureSmallTransfer(false), usingNumaControl(false),
      sendEncrypted(false)
{
#ifdef __linux__
    rlimit fdsLimit;
    getrlimit(RLIMIT_NOFILE, &fdsLimit);
    fdsLimit.rlim_cur = 4096;
    setrlimit(RLIMIT_NOFILE, &fdsLimit);
#endif

    Auth::CredentialsProviderChainDefaultConfig chainConfig;
    chainConfig.Bootstrap = &bootstrap;

    credsProvider = Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(chainConfig, traceAllocator);

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
        Logging,
        UsingNumaControl,
        SendEncrypted,
        MTU,

        MAX
    };

    const aws_cli_option options[] = {{"toolName", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 't'},
                                      {"instanceType", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'i'},
                                      {"cutOffTimeSmall", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'c'},
                                      {"cutOffTimeLarge", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'C'},
                                      {"measureLargeTransfer", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'l'},
                                      {"measureSmallTransfer", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 's'},
                                      {"logging", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'd'},
                                      {"usingNumaControl", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'n'},
                                      {"sendEncrypted", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'e'},
                                      {"mtu", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'm'}};

    const char *optstring = "t:i:c:C:lsdnem:";
    toolName = argc >= 1 ? argv[0] : "NA";

    size_t dirStart = toolName.rfind('\\');

    if (dirStart != String::npos)
    {
        toolName = toolName.substr(dirStart + 1);
    }

    int cliOptionIndex = 0;
    bool loggingOn = false;

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
            case CLIOption::Logging:
                loggingOn = true;
                break;
            case CLIOption::UsingNumaControl:
                usingNumaControl = true;
                break;
            case CLIOption::SendEncrypted:
                sendEncrypted = true;
                break;
            case CLIOption::MTU:
                mtu = atoi(aws_cli_optarg);
                break;
	    default:
                AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "Unknown CLI option used.");
                break;
        }
    }

    if (loggingOn)
    {
        apiHandle.InitializeLogging(LogLevel::Debug, stderr);

        // TODO Take out before merging--this is a giant hack to filter just canary logs
        aws_logger_vtable *currentVTable = aws_logger_get()->vtable;
        void **logFunctionVoid = (void **)&currentVTable->log;
        *logFunctionVoid = (void *)filterLog;
    }

    publisher = MakeShared<MetricsPublisher>(g_allocator, *this, "CRT-CPP-Canary");
    transport = MakeShared<S3ObjectTransport>(g_allocator, *this, "aws-crt-canary-bucket");
    measureTransferRate = MakeShared<MeasureTransferRate>(g_allocator, *this);
}
