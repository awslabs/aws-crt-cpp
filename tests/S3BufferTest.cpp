/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

/* Tests for downloading into, and uploading from, caller-owned memory:
 * S3GetObjectMetaRequestOptions::Create(request, ByteCursor) and
 * S3PutObjectMetaRequestOptions::Create(request, ByteCursor).
 *
 * The network tests need a bucket the default credentials can write to. They are skipped unless
 * CRT_S3_TEST_BUCKET_NAME is set; CRT_S3_TEST_REGION defaults to us-west-2. */

#include <aws/crt/Api.h>
#include <aws/crt/UUID.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/s3/S3.h>

#include <aws/common/environment.h>
#include <aws/testing/aws_test_harness.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <future>

using namespace Aws::Crt;

AWS_STATIC_STRING_FROM_LITERAL(s_bucketEnvName, "CRT_S3_TEST_BUCKET_NAME");
AWS_STATIC_STRING_FROM_LITERAL(s_regionEnvName, "CRT_S3_TEST_REGION");

static const uint64_t s_partSize = 8 * 1024 * 1024;
static const size_t s_objectSize = 20 * 1024 * 1024; /* more than two parts, so the upload is multipart */
static const uint8_t s_marker = 0xAB;

static Optional<String> s_GetEnv(Allocator *allocator, const aws_string *name)
{
    Optional<String> value;
    aws_string *output = nullptr;
    if (aws_get_environment_value(allocator, name, &output) == AWS_OP_SUCCESS && output != nullptr)
    {
        value = String(aws_string_c_str(output), output->len);
    }
    aws_string_destroy(output);
    return value;
}

/* Different in every 8 bytes, so a part landing in the wrong place is caught. */
static void s_Fill(uint8_t *data, size_t len)
{
    uint64_t x = 0x9E3779B97F4A7C15ull;
    size_t i = 0;
    for (; i + 8 <= len; i += 8)
    {
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        memcpy(data + i, &x, 8);
    }
    for (; i < len; ++i)
    {
        data[i] = static_cast<uint8_t>(i);
    }
}

/* A client plus the bucket, region and a unique key to use. */
struct S3TestEnv
{
    String bucket;
    String region;
    String key;
    String host;
    std::promise<void> clientShutdown;
    std::shared_ptr<S3::S3Client> client;
};

static int s_InitEnv(Allocator *allocator, S3TestEnv &env)
{
    Optional<String> bucket = s_GetEnv(allocator, s_bucketEnvName);
    if (!bucket.has_value())
    {
        printf("CRT_S3_TEST_BUCKET_NAME is not set, skipping\n");
        return AWS_OP_SKIP;
    }
    Optional<String> region = s_GetEnv(allocator, s_regionEnvName);
    env.bucket = bucket.value();
    env.region = region.has_value() ? region.value() : String("us-west-2");
    env.key = String("crt-cpp-zero-copy-test/") + String(UUID());
    env.host = env.bucket + ".s3." + env.region + ".amazonaws.com";

    Auth::CredentialsProviderChainDefaultConfig credentialsConfig;
    credentialsConfig.Bootstrap = ApiHandle::GetOrCreateStaticDefaultClientBootstrap();
    auto provider = Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(credentialsConfig, allocator);
    ASSERT_NOT_NULL(provider.get());

    S3::S3ClientConfig config(provider);
    config.SetRegion(env.region)
        .SetPartSize(s_partSize)
        .SetClientShutdownCallback([&env]() { env.clientShutdown.set_value(); });
    env.client = MakeShared<S3::S3Client>(allocator, config);
    ASSERT_TRUE(env.client && *env.client);
    return AWS_OP_SUCCESS;
}

static void s_CleanUpEnv(S3TestEnv &env)
{
    if (env.client)
    {
        env.client.reset();
        env.clientShutdown.get_future().wait();
    }
}

static std::shared_ptr<Http::HttpRequest> s_NewRequest(
    Allocator *allocator,
    const S3TestEnv &env,
    const char *method,
    const char *range)
{
    auto request = MakeShared<Http::HttpRequest>(allocator, allocator);
    request->SetMethod(ByteCursorFromCString(method));
    String path = String("/") + env.key;
    request->SetPath(ByteCursorFromString(path));
    Http::HttpHeader host = {};
    host.name = ByteCursorFromCString("Host");
    host.value = ByteCursorFromString(env.host);
    request->AddHeader(host);
    if (range != nullptr)
    {
        Http::HttpHeader rangeHeader = {};
        rangeHeader.name = ByteCursorFromCString("Range");
        rangeHeader.value = ByteCursorFromCString(range);
        request->AddHeader(rangeHeader);
    }
    return request;
}

/* Runs the meta request built from `options` and returns its result once it has fully shut down. If
 * destroyOptionsEarly is set, the options are destroyed right after MakeMetaRequest, as callers may. */
static int s_Run(
    S3TestEnv &env,
    ScopedResource<S3::S3MetaRequestOptions> options,
    bool destroyOptionsEarly,
    S3::S3MetaRequestResult &outResult)
{
    ASSERT_NOT_NULL(options.get());
    std::promise<S3::S3MetaRequestResult> finished;
    std::promise<void> shutdown;
    options->SetFinishCallback([&finished](const S3::S3MetaRequestResult &result) { finished.set_value(result); });
    options->SetShutdownCallback([&shutdown]() { shutdown.set_value(); });

    auto metaRequest = env.client->MakeMetaRequest(*options);
    if (!metaRequest)
    {
        printf("MakeMetaRequest failed: %s\n", aws_error_name(env.client->LastError()));
        return AWS_OP_ERR;
    }
    if (destroyOptionsEarly)
    {
        options.reset();
    }
    outResult = finished.get_future().get();
    metaRequest.reset();
    shutdown.get_future().wait();
    return AWS_OP_SUCCESS;
}

static int s_Upload(Allocator *allocator, S3TestEnv &env, uint8_t *data, size_t len)
{
    auto request = s_NewRequest(allocator, env, "PUT", nullptr);
    S3::S3MetaRequestResult result = {};
    ASSERT_SUCCESS(
        s_Run(env, S3::S3PutObjectMetaRequestOptions::Create(request, ByteCursorFromArray(data, len)), false, result));
    ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, result.errorCode);
    return AWS_OP_SUCCESS;
}

/* Downloads into a buffer of `capacity` bytes plus a guard area, all pre-filled with s_marker. */
struct GuardedBuffer
{
    explicit GuardedBuffer(size_t capacity) : capacity(capacity), mem(capacity + 64, s_marker) {}
    ByteCursor Cursor() { return ByteCursorFromArray(mem.data(), capacity); }
    bool UntouchedFrom(size_t from) const
    {
        for (size_t i = from; i < mem.size(); ++i)
        {
            if (mem[i] != s_marker)
            {
                return false;
            }
        }
        return true;
    }
    size_t capacity;
    Vector<uint8_t> mem;
};

/* Upload from memory, then download the whole object into a bigger buffer: same bytes, bytesReceived is the
 * object size, and nothing past it is written. The options are destroyed right after MakeMetaRequest, so this
 * also covers the aws_byte_buf living in the per-request data rather than the options. */
static int s_TestS3BufferRoundTrip(Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    {
        S3TestEnv env;
        int init = s_InitEnv(allocator, env);
        if (init != AWS_OP_SUCCESS)
        {
            return init;
        }

        Vector<uint8_t> source(s_objectSize);
        s_Fill(source.data(), source.size());
        ASSERT_SUCCESS(s_Upload(allocator, env, source.data(), source.size()));

        GuardedBuffer destination(s_objectSize + 1024 * 1024);
        auto request = s_NewRequest(allocator, env, "GET", nullptr);
        S3::S3MetaRequestResult result = {};
        ASSERT_SUCCESS(
            s_Run(env, S3::S3GetObjectMetaRequestOptions::Create(request, destination.Cursor()), true, result));
        ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, result.errorCode);
        ASSERT_UINT_EQUALS(s_objectSize, result.bytesReceived);
        ASSERT_INT_EQUALS(0, memcmp(destination.mem.data(), source.data(), s_objectSize));
        ASSERT_TRUE(destination.UntouchedFrom(s_objectSize));

        s_CleanUpEnv(env);
    }
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(S3BufferRoundTrip, s_TestS3BufferRoundTrip)

/* A ranged download lands at the start of the buffer, and a range with no end that fits works even with a
 * buffer smaller than a part. */
static int s_TestS3BufferRange(Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    {
        S3TestEnv env;
        int init = s_InitEnv(allocator, env);
        if (init != AWS_OP_SUCCESS)
        {
            return init;
        }

        Vector<uint8_t> source(s_objectSize);
        s_Fill(source.data(), source.size());
        ASSERT_SUCCESS(s_Upload(allocator, env, source.data(), source.size()));

        /* bytes=1000-9000099, crossing part boundaries */
        {
            const size_t start = 1000;
            const size_t len = 9000000 - 1000 + 100;
            GuardedBuffer destination(len);
            auto request = s_NewRequest(allocator, env, "GET", "bytes=1000-9000099");
            S3::S3MetaRequestResult result = {};
            ASSERT_SUCCESS(
                s_Run(env, S3::S3GetObjectMetaRequestOptions::Create(request, destination.Cursor()), false, result));
            ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, result.errorCode);
            ASSERT_UINT_EQUALS(len, result.bytesReceived);
            ASSERT_INT_EQUALS(0, memcmp(destination.mem.data(), source.data() + start, len));
            ASSERT_TRUE(destination.UntouchedFrom(len));
        }
        /* The last 100,000 bytes, with no end, into a 1 MiB buffer */
        {
            const size_t start = s_objectSize - 100000;
            GuardedBuffer destination(1024 * 1024);
            String range = String("bytes=") + std::to_string(start).c_str() + "-";
            auto request = s_NewRequest(allocator, env, "GET", range.c_str());
            S3::S3MetaRequestResult result = {};
            ASSERT_SUCCESS(
                s_Run(env, S3::S3GetObjectMetaRequestOptions::Create(request, destination.Cursor()), false, result));
            ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, result.errorCode);
            ASSERT_UINT_EQUALS(100000, result.bytesReceived);
            ASSERT_INT_EQUALS(0, memcmp(destination.mem.data(), source.data() + start, 100000));
            ASSERT_TRUE(destination.UntouchedFrom(100000));
        }

        s_CleanUpEnv(env);
    }
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(S3BufferRange, s_TestS3BufferRange)

/* A buffer too small for the object fails with AWS_ERROR_SHORT_BUFFER and reports no bytes. */
static int s_TestS3BufferTooSmall(Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    {
        S3TestEnv env;
        int init = s_InitEnv(allocator, env);
        if (init != AWS_OP_SUCCESS)
        {
            return init;
        }

        Vector<uint8_t> source(s_objectSize);
        s_Fill(source.data(), source.size());
        ASSERT_SUCCESS(s_Upload(allocator, env, source.data(), source.size()));

        GuardedBuffer destination(1024 * 1024);
        auto request = s_NewRequest(allocator, env, "GET", nullptr);
        S3::S3MetaRequestResult result = {};
        ASSERT_SUCCESS(
            s_Run(env, S3::S3GetObjectMetaRequestOptions::Create(request, destination.Cursor()), false, result));
        ASSERT_INT_EQUALS(AWS_ERROR_SHORT_BUFFER, result.errorCode);
        ASSERT_UINT_EQUALS(0, result.bytesReceived);

        s_CleanUpEnv(env);
    }
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(S3BufferTooSmall, s_TestS3BufferTooSmall)

/* No network: the upload overload fills in Content-Length from the body, and leaves one the caller set. */
static int s_TestS3BufferUploadContentLength(Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    {
        uint8_t data[100] = {};
        ByteCursor body = ByteCursorFromArray(data, sizeof(data));
        auto contentLengthOf = [](const Http::HttpRequest &request) -> String
        {
            for (size_t i = 0; i < request.GetHeaderCount(); ++i)
            {
                Optional<Http::HttpHeader> header = request.GetHeader(i);
                if (header.has_value() && aws_byte_cursor_eq_c_str_ignore_case(&header.value().name, "Content-Length"))
                {
                    return String(reinterpret_cast<const char *>(header.value().value.ptr), header.value().value.len);
                }
            }
            return String();
        };

        auto request = MakeShared<Http::HttpRequest>(allocator, allocator);
        auto options = S3::S3PutObjectMetaRequestOptions::Create(request, body);
        ASSERT_NOT_NULL(options.get());
        ASSERT_TRUE(contentLengthOf(*request) == "100");

        auto requestWithLength = MakeShared<Http::HttpRequest>(allocator, allocator);
        Http::HttpHeader header = {};
        header.name = ByteCursorFromCString("Content-Length");
        header.value = ByteCursorFromCString("100");
        requestWithLength->AddHeader(header);
        auto optionsWithLength = S3::S3PutObjectMetaRequestOptions::Create(requestWithLength, body);
        ASSERT_NOT_NULL(optionsWithLength.get());
        ASSERT_UINT_EQUALS(1, requestWithLength->GetHeaderCount());
    }
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(S3BufferUploadContentLength, s_TestS3BufferUploadContentLength)

/* No network: a buffer download that's cancelled right away. The options are destroyed right after MakeMetaRequest,
 * the FinishCallback reports an error with bytesReceived 0, nothing is written to the buffer, and the request and the
 * client both shut down (the leak check also covers the aws_byte_buf held in the per-request data). */
static int s_TestS3BufferDownloadCancelled(Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    {
        Auth::CredentialsProviderStaticConfig credentialsConfig;
        credentialsConfig.AccessKeyId = ByteCursorFromCString("AKIDEXAMPLE");
        credentialsConfig.SecretAccessKey = ByteCursorFromCString("wJalrXUtnFEMI/K7MDENG+bPxRfiCYEXAMPLEKEY");
        auto provider = Auth::CredentialsProvider::CreateCredentialsProviderStatic(credentialsConfig, allocator);
        ASSERT_NOT_NULL(provider.get());

        std::promise<void> clientShutdown;
        S3::S3ClientConfig config(provider);
        config.SetRegion("us-east-1")
            .SetTlsMode(S3::S3TlsMode::Disabled)
            .SetClientShutdownCallback([&clientShutdown]() { clientShutdown.set_value(); });
        auto client = MakeShared<S3::S3Client>(allocator, config);
        ASSERT_TRUE(client && *client);

        auto request = MakeShared<Http::HttpRequest>(allocator, allocator);
        request->SetMethod(ByteCursorFromCString("GET"));
        request->SetPath(ByteCursorFromCString("/key"));
        Http::HttpHeader host = {};
        host.name = ByteCursorFromCString("Host");
        host.value = ByteCursorFromCString("localhost");
        request->AddHeader(host);

        GuardedBuffer destination(1024 * 1024);
        auto options = S3::S3GetObjectMetaRequestOptions::Create(request, destination.Cursor());
        ASSERT_NOT_NULL(options.get());
        std::promise<std::pair<int, uint64_t>> finished;
        std::promise<void> shutdown;
        options->SetFinishCallback([&finished](const S3::S3MetaRequestResult &result)
                                   { finished.set_value(std::make_pair(result.errorCode, result.bytesReceived)); });
        options->SetShutdownCallback([&shutdown]() { shutdown.set_value(); });

        auto metaRequest = client->MakeMetaRequest(*options);
        ASSERT_NOT_NULL(metaRequest.get());
        metaRequest->Cancel();
        options.reset();

        auto finishedFuture = finished.get_future();
        ASSERT_TRUE(finishedFuture.wait_for(std::chrono::seconds(60)) == std::future_status::ready);
        std::pair<int, uint64_t> result = finishedFuture.get();
        ASSERT_FALSE(result.first == AWS_ERROR_SUCCESS);
        ASSERT_UINT_EQUALS(0, result.second);

        metaRequest.reset();
        ASSERT_TRUE(shutdown.get_future().wait_for(std::chrono::seconds(30)) == std::future_status::ready);
        ASSERT_TRUE(destination.UntouchedFrom(0));

        client.reset();
        ASSERT_TRUE(clientShutdown.get_future().wait_for(std::chrono::seconds(30)) == std::future_status::ready);
    }
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(S3BufferDownloadCancelled, s_TestS3BufferDownloadCancelled)
