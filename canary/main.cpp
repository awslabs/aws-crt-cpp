/*
 * Copyright 2010-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include "CanaryUtil.h"
#include "MeasureTransferRate.h"
#include "MetricsPublisher.h"
#include "S3ObjectTransport.h"
#include <aws/common/log_channel.h>
#include <aws/common/log_formatter.h>
#include <aws/common/logging.h>
#include <aws/crt/Api.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/io/stream.h>
#include <time.h>

using namespace Aws::Crt;

int main(int argc, char *argv[])
{
    Allocator *traceAllocator = aws_mem_tracer_new(DefaultAllocator(), NULL, AWS_MEMTRACE_BYTES, 0);
    ApiHandle apiHandle(traceAllocator);
    apiHandle.InitializeLogging(LogLevel::Info, stderr);

    Io::EventLoopGroup eventLoopGroup(traceAllocator);
    Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 60, 1000, traceAllocator);
    Io::ClientBootstrap bootstrap(eventLoopGroup, defaultHostResolver, traceAllocator);

    auto contextOptions = Io::TlsContextOptions::InitDefaultClient(traceAllocator);
    Io::TlsContext tlsContext(contextOptions, Io::TlsMode::CLIENT, traceAllocator);

    Auth::CredentialsProviderChainDefaultConfig chainConfig;
    chainConfig.Bootstrap = &bootstrap;

    auto credsProvider = Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(chainConfig, traceAllocator);
    auto signer = MakeShared<Auth::Sigv4HttpRequestSigner>(traceAllocator, traceAllocator);

    String platformName = CanaryUtil::GetPlatformName();

    String toolName = argc >= 1 ? argv[0] : "NA";
    size_t dirStart = toolName.rfind('\\');

    if (dirStart != String::npos)
    {
        toolName = toolName.substr(dirStart + 1);
    }

    CanaryUtil::GetSwitchVariable(argc, argv, "-toolName", toolName);

    String ec2InstanceType = "NA";
    CanaryUtil::GetSwitchVariable(argc, argv, "-ec2InstanceType", ec2InstanceType);

    double cutOffTimeSmallObjects = 10.0;
    double cutOffTimeLargeObjects = 10.0;

    String cutOffTimeString;
    if (CanaryUtil::GetSwitchVariable(argc, argv, "-cutOffTimeSmallObjects", cutOffTimeString))
    {
        cutOffTimeSmallObjects = atof(cutOffTimeString.c_str());
    }

    if (CanaryUtil::GetSwitchVariable(argc, argv, "-cutOffTimeLargeObjects", cutOffTimeString))
    {
        cutOffTimeLargeObjects = atof(cutOffTimeString.c_str());
    }

    MetricsPublisher publisher(
        platformName,
        toolName,
        ec2InstanceType,
        "us-west-2",
        tlsContext,
        bootstrap,
        eventLoopGroup,
        credsProvider,
        signer);
    publisher.Namespace = "CRT-CPP-Canary";

    S3ObjectTransport transport(
        eventLoopGroup, "us-west-2", "aws-crt-canary-bucket", tlsContext, bootstrap, credsProvider, signer);

    MeasureTransferRate measureTransferRate;

    if (CanaryUtil::HasSwitch(argc, argv, "-smallObjectTransfer"))
    {
        publisher.SetMetricTransferSize(MetricTransferSize::Small);

        measureTransferRate.MeasureSmallObjectTransfer(traceAllocator, transport, publisher, cutOffTimeSmallObjects);

        publisher.WaitForLastPublish();
    }

    if (CanaryUtil::HasSwitch(argc, argv, "-largeObjectTransfer"))
    {
        publisher.SetMetricTransferSize(MetricTransferSize::Large);

        measureTransferRate.MeasureLargeObjectTransfer(traceAllocator, transport, publisher, cutOffTimeLargeObjects);

        publisher.WaitForLastPublish();
    }

    return 0;
}
