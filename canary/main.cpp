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

#include <aws/crt/Api.h>
#include <aws/crt/auth/Credentials.h>
#include "MetricsPublisher.h"

using namespace Aws::Crt;

int main() {
    Allocator *traceAllocator = aws_mem_tracer_new(DefaultAllocator(), NULL, AWS_MEMTRACE_BYTES, 0);
    ApiHandle apiHandle(traceAllocator);
    apiHandle.InitializeLogging(LogLevel::Trace, stderr);

    Io::EventLoopGroup eventLoopGroup(traceAllocator);
    Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 2, 1000, traceAllocator);
    Io::ClientBootstrap bootstrap(eventLoopGroup, defaultHostResolver, traceAllocator);

    auto contextOptions = Io::TlsContextOptions::InitDefaultClient(traceAllocator);
    Io::TlsContext tlsContext(contextOptions, Io::TlsMode::CLIENT, traceAllocator);

    Auth::CredentialsProviderChainDefaultConfig chainConfig;
    chainConfig.Bootstrap = &bootstrap;

    auto credsProvider = Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(chainConfig, traceAllocator);
    auto signer = MakeShared<Auth::Sigv4HttpRequestSigner>(traceAllocator, traceAllocator);

    MetricsPublisher publisher("us-west-2", tlsContext, bootstrap, eventLoopGroup, credsProvider, signer);
    publisher.Namespace = "CRT-CPP-Canary";

    while (true) {
        Metric metric;
        metric.Unit = MetricUnit::Count;
        metric.Value = 1;
        metric.Timestamp = DateTime::Now();
        metric.MetricName = "LoopIteration";
        publisher.AddDataPoint(metric);

        Metric memMetric;
        memMetric.Unit = MetricUnit::Bytes;
        memMetric.Value = (double)aws_mem_tracer_bytes(traceAllocator);
        memMetric.Timestamp = DateTime::Now();
        memMetric.MetricName = "BytesAllocated";
        publisher.AddDataPoint(memMetric);

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    return 0;
}