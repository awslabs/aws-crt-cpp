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

CanaryAppOptions::CanaryAppOptions() noexcept
    : platformName(CanaryUtil::GetPlatformName()), toolName("NA"), instanceType("unknown"), region("us-west-2"),
      bucketName("aws-crt-canary-bucket"), readFromParentPipe(-1), writeToParentPipe(-1), numUpTransfers(1),
      numUpConcurrentTransfers(0), numDownTransfers(1), numDownConcurrentTransfers(0), childProcessIndex(0),
      measureSinglePartTransfer(false), measureMultiPartTransfer(false), measureHttpTransfer(false),
      usingNumaControl(false), downloadOnly(false), sendEncrypted(false), loggingEnabled(false), rehydrateBackup(false),
      forkModeEnabled(false), isParentProcess(false), isChildProcess(false)
{
}

#ifndef WIN32
CanaryAppChildProcess::CanaryAppChildProcess() noexcept : pid(0), readFromChildPipe(-1), writeToChildPipe(-1) {}

CanaryAppChildProcess::CanaryAppChildProcess(pid_t inPid, int32_t inReadPipe, int32_t inWritePipe) noexcept
    : pid(inPid), readFromChildPipe(inReadPipe), writeToChildPipe(inWritePipe)
{
}
#endif

CanaryApp::CanaryApp(CanaryAppOptions &&inOptions) noexcept
    : m_options(inOptions), m_apiHandle(g_allocator),
      m_eventLoopGroup((!inOptions.isChildProcess && !inOptions.isParentProcess) ? 72 : 2, g_allocator),
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

    m_publisher = MakeShared<MetricsPublisher>(g_allocator, *this, MetricNamespace);
    m_uploadTransport = MakeShared<S3ObjectTransport>(g_allocator, *this, m_options.bucketName.c_str());
    m_downloadTransport = MakeShared<S3ObjectTransport>(g_allocator, *this, m_options.bucketName.c_str());
    m_measureTransferRate = MakeShared<MeasureTransferRate>(g_allocator, *this);
}

#ifndef WIN32
CanaryApp::CanaryApp(CanaryAppOptions &&inOptions, std::vector<CanaryAppChildProcess> &&inChildren) noexcept
    : CanaryApp(std::move(inOptions))
{
    children = inChildren;
}
#endif

void CanaryApp::WriteToChildProcess(uint32_t index, const char *key, const char *value)
{
#ifndef WIN32
    const CanaryAppChildProcess &child = children[index];

    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY, "Writing %s:%s to child %d through pipe %d", key, value, index, child.writeToChildPipe);

    WriteKeyValueToPipe(key, value, child.writeToChildPipe);
#else
    (void)index;
    (void)key;
    (void)value;

    AWS_FATAL_ASSERT(false);
#endif
}

void CanaryApp::WriteToParentProcess(const char *key, const char *value)
{
#ifndef WIN32
    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY, "Writing %s:%s to parent through pipe %d", key, value, m_options.writeToParentPipe);

    WriteKeyValueToPipe(key, value, m_options.writeToParentPipe);
#else
    (void)key;
    (void)value;

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
    (void)index;
    (void)key;

    AWS_FATAL_ASSERT(false);
    return "";
#endif
}

String CanaryApp::ReadFromParentProcess(const char *key)
{
#ifndef WIN32
    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY, "Reading value of %s from parent through pipe %d...", key, m_options.readFromParentPipe);

    String value = ReadValueFromPipe(key, m_options.readFromParentPipe, valuesFromParent);

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Got value %s from parent", value.c_str());

    return value;
#else
    (void)key;
    AWS_FATAL_ASSERT(false);
    return "";
#endif
}

#ifndef WIN32

void CanaryApp::WriteKeyValueToPipe(const char *key, const char *value, uint32_t writePipe)
{
    const char nullTerm = '\0';

    if (write(writePipe, key, strlen(key)) == -1)
    {
        AWS_LOGF_FATAL(AWS_LS_CRT_CPP_CANARY, "Writing key to pipe failed.");
        exit(EXIT_FAILURE);
        return;
    }

    if (write(writePipe, &nullTerm, 1) == -1)
    {
        AWS_LOGF_FATAL(AWS_LS_CRT_CPP_CANARY, "Writing key null terminator to pipe failed.");
        exit(EXIT_FAILURE);
        return;
    }

    if (write(writePipe, value, strlen(value)) == -1)
    {
        AWS_LOGF_FATAL(AWS_LS_CRT_CPP_CANARY, "Writing value to pipe failed.");
        exit(EXIT_FAILURE);
        return;
    }

    if (write(writePipe, &nullTerm, 1) == -1)
    {
        AWS_LOGF_FATAL(AWS_LS_CRT_CPP_CANARY, "Writing value null terminator to pipe failed.");
        exit(EXIT_FAILURE);
        return;
    }
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

    if (m_options.readFromParentPipe != -1)
    {
        close(m_options.readFromParentPipe);
        m_options.readFromParentPipe = -1;
    }

    if (m_options.writeToParentPipe != -1)
    {
        close(m_options.writeToParentPipe);
        m_options.writeToParentPipe = -1;
    }

    children.clear();
#endif
}