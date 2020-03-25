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

#ifndef WIN32
#    include <sys/resource.h>
#    include <sys/types.h>
#    include <sys/wait.h>
#    include <unistd.h>
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

CanaryAppOptions::CanaryAppOptions() noexcept
    : platformName(CanaryUtil::GetPlatformName()), toolName("NA"), instanceType("unknown"), region("us-west-2"),
      readFromParentPipe(-1), writeToParentPipe(-1), mtu(0), numUpTransfers(1), numUpConcurrentTransfers(0),
      numDownTransfers(1), numDownConcurrentTransfers(0), childProcessIndex(0), measureLargeTransfer(false),
      measureSmallTransfer(false), measureHttpTransfer(false), usingNumaControl(false), downloadOnly(false),
      sendEncrypted(false), loggingEnabled(false), rehydrateBackup(false), isParentProcess(false), isChildProcess(false)
{
}

CanaryAppChildProcess::CanaryAppChildProcess() noexcept : pid(0), readFromChildPipe(-1), writeToChildPipe(-1) {}

CanaryAppChildProcess::CanaryAppChildProcess(pid_t inPid, int32_t inReadPipe, int32_t inWritePipe) noexcept
    : pid(inPid), readFromChildPipe(inReadPipe), writeToChildPipe(inWritePipe)
{
}

CanaryApp::CanaryApp(CanaryAppOptions &&inOptions, std::vector<CanaryAppChildProcess> &&inChildren) noexcept
    : traceAllocator(DefaultAllocator()), apiHandle(traceAllocator),
      eventLoopGroup((!inOptions.isChildProcess && !inOptions.isParentProcess) ? 72 : 2, traceAllocator),
      defaultHostResolver(eventLoopGroup, 60, 3600, traceAllocator),
      bootstrap(eventLoopGroup, defaultHostResolver, traceAllocator), options(inOptions), children(inChildren)
{
#ifndef WIN32
    rlimit fdsLimit;
    getrlimit(RLIMIT_NOFILE, &fdsLimit);
    fdsLimit.rlim_cur = 8192;
    setrlimit(RLIMIT_NOFILE, &fdsLimit);
#endif

    if (options.loggingEnabled)
    {
        apiHandle.InitializeLogging(LogLevel::Info, stderr);

        // TODO Take out before merging--this is a giant hack to filter just canary logs
        aws_logger_vtable *currentVTable = aws_logger_get()->vtable;
        void **logFunctionVoid = (void **)&currentVTable->log;
        *logFunctionVoid = (void *)filterLog;
    }

    Auth::CredentialsProviderChainDefaultConfig chainConfig;
    chainConfig.Bootstrap = &bootstrap;

    credsProvider = Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(chainConfig, g_allocator);

    signer = MakeShared<Auth::Sigv4HttpRequestSigner>(g_allocator, g_allocator);

    Io::TlsContextOptions tlsContextOptions = Io::TlsContextOptions::InitDefaultClient(g_allocator);
    tlsContext = Io::TlsContext(tlsContextOptions, Io::TlsMode::CLIENT, g_allocator);

    publisher = MakeShared<MetricsPublisher>(g_allocator, *this, "CRT-CPP-Canary-V2");
    transport = MakeShared<S3ObjectTransport>(g_allocator, *this, "aws-crt-canary-bucket");
    transportSecondary = MakeShared<S3ObjectTransport>(g_allocator, *this, "aws-crt-test-stuff-us-west-2");
    measureTransferRate = MakeShared<MeasureTransferRate>(g_allocator, *this);
}

void CanaryApp::WriteToChildProcess(uint32_t index, const char *key, const char *value)
{
#ifndef WIN32
    const CanaryAppChildProcess &child = children[index]; // TODO bounds fatal assert?

    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY, "Writing %s:%s to child %d through pipe %d", key, value, index, child.writeToChildPipe);

    WriteKeyValueToPipe(key, value, child.writeToChildPipe);
#else
    AWS_FATAL_ASSERT(false);
#endif
}

void CanaryApp::WriteToParentProcess(const char *key, const char *value)
{
#ifndef WIN32
    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY, "Writing %s:%s to parent through pipe %d", key, value, options.writeToParentPipe);

    WriteKeyValueToPipe(key, value, options.writeToParentPipe);
#else
    AWS_FATAL_ASSERT(false);
#endif
}

String CanaryApp::ReadFromChildProcess(uint32_t index, const char *key)
{
#ifndef WIN32
    CanaryAppChildProcess &child = children[index];

    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY,
        "Reading value of %s from child %d through pipe %d...",
        key,
        index,
        child.readFromChildPipe);

    String value = ReadValueFromPipe(key, child.readFromChildPipe, child.valuesFromChild);

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Got value %s from child %d", value.c_str(), index);

    return value;
#else
    AWS_FATAL_ASSERT(false);
    return "";
#endif
}

String CanaryApp::ReadFromParentProcess(const char *key)
{
#ifndef WIN32
    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY, "Reading value of %s from parent through pipe %d...", key, options.readFromParentPipe);

    String value = ReadValueFromPipe(key, options.readFromParentPipe, valuesFromParent);

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Got value %s from parent", value.c_str());

    return value;
#else
    AWS_FATAL_ASSERT(false);
    return "";
#endif
}

#ifndef WIN32

void CanaryApp::WriteKeyValueToPipe(const char *key, const char *value, uint32_t writePipe)
{
    const char nullTerm = '\0';

    write(writePipe, key, strlen(key));
    write(writePipe, &nullTerm, 1);
    write(writePipe, value, strlen(value));
    write(writePipe, &nullTerm, 1);
}

String CanaryApp::ReadValueFromPipe(const char *key, int32_t readPipe, std::map<String, String> &keyValuePairs)
{
    auto it = keyValuePairs.find(key);

    if (it != keyValuePairs.end())
    {
        return it->second;
    }

    std::pair<String, String> keyValuePair;

    do
    {
        keyValuePair = ReadNextKeyValuePairFromPipe(readPipe);
        keyValuePairs.insert(keyValuePair);

    } while (keyValuePair.first != key);

    return keyValuePair.second;
}

std::pair<String, String> CanaryApp::ReadNextKeyValuePairFromPipe(int32_t readPipe)
{
    char c;
    String currentBuffer;
    std::pair<String, String> keyValuePair;
    uint32_t index = 0;

    while (index < 2)
    {
        int32_t readResult = read(readPipe, &c, 1);

        if (readResult == -1)
        {
            AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "Read returned error %d", readResult);
            break;
        }

        if (readResult == 0)
        {
            continue;
        }

        if (c == '\0')
        {
            if (index == 0)
            {
                keyValuePair.first = std::move(currentBuffer);
            }
            else
            {
                keyValuePair.second = std::move(currentBuffer);
            }

            ++index;
        }
        else
        {
            currentBuffer += c;
        }
    }

    return keyValuePair;
}

#endif

void CanaryApp::Run()
{
    if (options.rehydrateBackup)
    {
        publisher->RehydrateBackup(options.rehydrateBackupObjectName.c_str());
    }

    if (options.measureSmallTransfer)
    {
        publisher->SetMetricTransferSize(MetricTransferSize::Small);
        measureTransferRate->MeasureSmallObjectTransfer();
    }

    if (options.measureLargeTransfer)
    {
        publisher->SetMetricTransferSize(MetricTransferSize::Large);
        measureTransferRate->MeasureLargeObjectTransfer();
    }

    if (options.measureHttpTransfer)
    {
        publisher->SetMetricTransferSize(MetricTransferSize::Small);
        measureTransferRate->MeasureHttpTransfer();
    }

#ifndef WIN32
    for (CanaryAppChildProcess &childProcess : children)
    {
        if (childProcess.readFromChildPipe != -1)
        {
            close(childProcess.readFromChildPipe);
            childProcess.readFromChildPipe = -1;
        }

        if (childProcess.writeToChildPipe != -1)
        {
            close(childProcess.writeToChildPipe);
            childProcess.writeToChildPipe = -1;
        }
    }

    if (options.readFromParentPipe != -1)
    {
        close(options.readFromParentPipe);
        options.readFromParentPipe = -1;
    }

    if (options.writeToParentPipe != -1)
    {
        close(options.writeToParentPipe);
        options.writeToParentPipe = -1;
    }

    children.clear();
#endif
}