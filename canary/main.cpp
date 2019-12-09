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

#include "MetricsPublisher.h"
#include "S3ObjectTransport.h"
#include <aws/common/system_info.h>
#include <aws/crt/Api.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/io/stream.h>

using namespace Aws::Crt;

static const char s_BodyTemplate[] =
    "This is a test string for use with canary testing against Amazon Simple Storage Service";
static size_t s_defaultObjSize = 16 * 1024 * 1024;

struct TemplateStream
{
    aws_input_stream inputStream;
    MetricsPublisher *publisher;
    size_t length;
    size_t written;
};

static int s_templateStreamRead(struct aws_input_stream *stream, struct aws_byte_buf *dest)
{
    auto templateStream = static_cast<TemplateStream *>(stream->impl);

    size_t totalBufferSpace = dest->capacity - dest->len;
    size_t unwritten = templateStream->length - templateStream->written;
    size_t totalToWrite = totalBufferSpace > unwritten ? unwritten : totalBufferSpace;
    size_t writtenOut = 0;

    while (totalToWrite)
    {
        size_t toWrite =
            AWS_ARRAY_SIZE(s_BodyTemplate) - 1 > totalToWrite ? totalToWrite : AWS_ARRAY_SIZE(s_BodyTemplate) - 1;
        ByteCursor outCur = ByteCursorFromArray((const uint8_t *)s_BodyTemplate, toWrite);

        aws_byte_buf_append(dest, &outCur);

        writtenOut += toWrite;
        totalToWrite = totalToWrite - toWrite;
    }

    templateStream->written += writtenOut;

    if (templateStream->length == templateStream->written)
    {
        Metric uploadMetric;
        uploadMetric.MetricName = "BytesUp";
        uploadMetric.Timestamp = DateTime::Now();
        uploadMetric.Value = (double)templateStream->length;
        uploadMetric.Unit = MetricUnit::Bytes;

        templateStream->publisher->AddDataPoint(uploadMetric);
    }

    return AWS_OP_SUCCESS;
}

static int s_templateStreamGetStatus(struct aws_input_stream *stream, struct aws_stream_status *status)
{
    auto templateStream = static_cast<TemplateStream *>(stream->impl);

    status->is_end_of_stream = templateStream->written == templateStream->length;
    status->is_valid = !status->is_end_of_stream;

    return AWS_OP_SUCCESS;
}

static int s_templateStreamSeek(struct aws_input_stream *stream, aws_off_t offset, enum aws_stream_seek_basis basis)
{
    (void)offset;
    (void)basis;

    auto templateStream = static_cast<TemplateStream *>(stream->impl);
    templateStream->written = 0;
    return AWS_OP_SUCCESS;
}

static int s_templateStreamGetLength(struct aws_input_stream *stream, int64_t *length)
{
    auto templateStream = static_cast<TemplateStream *>(stream->impl);
    *length = templateStream->length;
    return AWS_OP_SUCCESS;
}

static void s_templateStreamDestroy(struct aws_input_stream *stream)
{
    auto templateStream = static_cast<TemplateStream *>(stream->impl);
    Delete(templateStream, stream->allocator);
}

static struct aws_input_stream_vtable s_templateStreamVTable = {s_templateStreamSeek,
                                                                s_templateStreamRead,
                                                                s_templateStreamGetStatus,
                                                                s_templateStreamGetLength,
                                                                s_templateStreamDestroy};

static aws_input_stream *s_createTemplateStream(Allocator *alloc, MetricsPublisher *publisher, size_t length)
{
    auto templateStream = New<TemplateStream>(alloc);
    templateStream->publisher = publisher;
    templateStream->length = length;
    templateStream->written = 0;
    templateStream->inputStream.allocator = alloc;
    templateStream->inputStream.impl = templateStream;
    templateStream->inputStream.vtable = &s_templateStreamVTable;

    return &templateStream->inputStream;
}

int main()
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

    MetricsPublisher publisher("us-west-2", tlsContext, bootstrap, eventLoopGroup, credsProvider, signer);
    publisher.Namespace = "CRT-CPP-Canary";

    size_t threadCount = aws_system_info_processor_count();
    size_t maxInFlight = threadCount * 10;
    S3ObjectTransport transport(
        "us-west-2", "aws-crt-canary-bucket", tlsContext, bootstrap, credsProvider, signer, maxInFlight);

/*
    StringStream keyStream1;
    keyStream1 << "test_large_1_" << GetTickCount();
    auto key1 = keyStream1.str();
    uint64_t objectSize1 = S3ObjectTransport::MaxPartSizeBytes * 10;

    transport.PutObjectMultipart(
        key1,
        objectSize1,
        [&traceAllocator, &publisher](uint64_t byteStart, uint64_t byteSize) {
            (void)byteStart;

            aws_input_stream *inputStream = s_createTemplateStream(traceAllocator, &publisher, byteSize);
            return inputStream;
        },
        [](int errorCode) {
            (void)errorCode;
        });

    StringStream keyStream2;
    keyStream2 << "test_large_2_" << GetTickCount();
    auto key2 = keyStream2.str();
    uint64_t objectSize2 = S3ObjectTransport::MaxPartSizeBytes * 5;

    transport.PutObjectMultipart(
        key2,
        objectSize2,
        [&traceAllocator, &publisher](uint64_t byteStart, uint64_t byteSize) {
            (void)byteStart;

            aws_input_stream *inputStream = s_createTemplateStream(traceAllocator, &publisher, byteSize);
            return inputStream;
        },
        [](int errorCode) { (void)errorCode; });
*/   

    bool shouldContinue = true;
    uint64_t counter = INT64_MAX;
    std::atomic<size_t> inFlight(0);

    while (shouldContinue)
    {

        if (counter == 0)
        {
            counter = INT64_MAX;
        }

        while (inFlight < maxInFlight)
        {
            StringStream keyStream;
            keyStream << "crt-canary-obj-" << counter--;
            inFlight += 1;
            auto key = keyStream.str();
            transport.PutObject(
                key, s_createTemplateStream(traceAllocator, &publisher, s_defaultObjSize), 0, [&, key](int errorCode,
    std::shared_ptr<Aws::Crt::String> etag) { if (errorCode == AWS_OP_SUCCESS)
                    {
                        Metric successMetric;
                        successMetric.MetricName = "SuccessfulTransfer";
                        successMetric.Unit = MetricUnit::Count;
                        successMetric.Value = 1;
                        successMetric.Timestamp = DateTime::Now();

                        publisher.AddDataPoint(successMetric);

                        auto downMetric = New<Metric>(g_allocator);
                        downMetric->MetricName = "BytesDown";
                        downMetric->Unit = MetricUnit::Bytes;

                        transport.GetObject(
                            key,
                            [&, downMetric](Http::HttpStream &, const ByteCursor &cur) {
                                downMetric->Value += (double)cur.len;
                            },
                            [&, downMetric](int errorCode) {
                                if (errorCode == AWS_OP_SUCCESS)
                                {
                                    Metric successMetric;
                                    successMetric.MetricName = "SuccessfulTransfer";
                                    successMetric.Unit = MetricUnit::Count;
                                    successMetric.Value = 1;
                                    successMetric.Timestamp = DateTime::Now();

                                    publisher.AddDataPoint(successMetric);
                                }
                                else
                                {
                                    Metric failureMetric;
                                    failureMetric.MetricName = "FailedTransfer";
                                    failureMetric.Unit = MetricUnit::Count;
                                    failureMetric.Value = 1;
                                    failureMetric.Timestamp = DateTime::Now();

                                    publisher.AddDataPoint(failureMetric);
                                }

                                downMetric->Timestamp = DateTime::Now();
                                publisher.AddDataPoint(*downMetric);
                                Delete(downMetric, g_allocator);

                                inFlight -= 1;
                            });
                    }
                    else
                    {
                        Metric failureMetric;
                        failureMetric.MetricName = "FailedTransfer";
                        failureMetric.Unit = MetricUnit::Count;
                        failureMetric.Value = 1;
                        failureMetric.Timestamp = DateTime::Now();

                        publisher.AddDataPoint(failureMetric);
                        inFlight -= 1;
                    }
                });
        }

        Metric memMetric;
        memMetric.Unit = MetricUnit::Bytes;
        memMetric.Value = (double)aws_mem_tracer_bytes(traceAllocator);
        memMetric.Timestamp = DateTime::Now();
        memMetric.MetricName = "BytesAllocated";
        publisher.AddDataPoint(memMetric);
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}