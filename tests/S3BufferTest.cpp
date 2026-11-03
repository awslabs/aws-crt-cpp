/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

/* Tests for downloading into, and uploading from, caller-owned memory:
 * S3GetObjectMetaRequestOptions::Create(request, ByteBuf &) and
 * S3PutObjectMetaRequestOptions::Create(request, ByteCursor).
 *
 * The network tests need a bucket the default credentials can write to. They are skipped unless
 * CRT_S3_TEST_BUCKET_NAME is set; CRT_S3_TEST_REGION defaults to us-west-2. Each one deletes the object it uploads. */

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
static const size_t s_smallObjectSize = 1000;        /* below the multipart threshold, so a single PUT */
static const uint8_t s_marker = 0xAB;                /* pre-fills every destination, so writes show up */
static const uint8_t s_data = 0x5C;                  /* the uploaded bytes */

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

/* A client plus the bucket, region and a unique key to use. */
struct S3TestEnv
{
    String bucket;
    String region;
    String key;
    String host;
    bool uploaded = false;
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

/* Deletes the uploaded object, if any, then shuts the client down. */
static int s_CleanUpEnv(Allocator *allocator, S3TestEnv &env)
{
    if (!env.client)
    {
        return AWS_OP_SUCCESS;
    }
    if (env.uploaded)
    {
        auto request = s_NewRequest(allocator, env, "DELETE", nullptr);
        S3::S3MetaRequestResult result = {};
        ASSERT_SUCCESS(
            s_Run(env, S3::S3DefaultObjectMetaRequestOptions::Create(request, "DeleteObject"), false, result));
        ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, result.errorCode);
        env.uploaded = false;
    }
    env.client.reset();
    env.clientShutdown.get_future().wait();
    return AWS_OP_SUCCESS;
}

/* Uploads `data` with no Content-Length header: aws-c-s3 takes the length from the body. */
static int s_Upload(Allocator *allocator, S3TestEnv &env, const Vector<uint8_t> &data)
{
    auto request = s_NewRequest(allocator, env, "PUT", nullptr);
    S3::S3MetaRequestResult result = {};
    ASSERT_SUCCESS(s_Run(
        env,
        S3::S3PutObjectMetaRequestOptions::Create(request, ByteCursorFromArray(data.data(), data.size())),
        false,
        result));
    ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, result.errorCode);
    env.uploaded = true;
    return AWS_OP_SUCCESS;
}

/* A destination of `capacity` bytes plus a guard area, all pre-filled with s_marker, and the caller-owned ByteBuf
 * (len 0) viewing the first `capacity` bytes. */
struct GuardedBuffer
{
    explicit GuardedBuffer(size_t capacity)
        : mem(capacity + 64, s_marker), buf(ByteBufFromEmptyArray(mem.data(), capacity))
    {
    }
    GuardedBuffer(const GuardedBuffer &) = delete;
    GuardedBuffer &operator=(const GuardedBuffer &) = delete;

    /* Every byte in [0, n) was written with s_data, and nothing from n on (including the guard area) was touched. */
    bool HoldsDataUpTo(size_t n) const
    {
        for (size_t i = 0; i < mem.size(); ++i)
        {
            if (mem[i] != (i < n ? s_data : s_marker))
            {
                return false;
            }
        }
        return true;
    }

    Vector<uint8_t> mem;
    ByteBuf buf;
};

/* Upload from memory, then download the whole object into a bigger buffer: len is the object size, [0, len) is
 * filled and nothing past it is written. Done for a multipart upload and a single-PUT one, neither with a
 * Content-Length header. The options are destroyed right after MakeMetaRequest, since only the ByteBuf has to
 * outlive the request. */
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

        for (size_t size : {s_objectSize, s_smallObjectSize})
        {
            ASSERT_SUCCESS(s_Upload(allocator, env, Vector<uint8_t>(size, s_data)));

            GuardedBuffer destination(size + 1024 * 1024);
            auto request = s_NewRequest(allocator, env, "GET", nullptr);
            S3::S3MetaRequestResult result = {};
            ASSERT_SUCCESS(
                s_Run(env, S3::S3GetObjectMetaRequestOptions::Create(request, destination.buf), true, result));
            ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, result.errorCode);
            ASSERT_UINT_EQUALS(size, destination.buf.len);
            ASSERT_TRUE(destination.HoldsDataUpTo(size));
        }

        ASSERT_SUCCESS(s_CleanUpEnv(allocator, env));
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

        ASSERT_SUCCESS(s_Upload(allocator, env, Vector<uint8_t>(s_objectSize, s_data)));

        /* bytes=1000-9000099, crossing part boundaries */
        {
            const size_t len = 9000000 - 1000 + 100;
            GuardedBuffer destination(len);
            auto request = s_NewRequest(allocator, env, "GET", "bytes=1000-9000099");
            S3::S3MetaRequestResult result = {};
            ASSERT_SUCCESS(
                s_Run(env, S3::S3GetObjectMetaRequestOptions::Create(request, destination.buf), false, result));
            ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, result.errorCode);
            ASSERT_UINT_EQUALS(len, destination.buf.len);
            ASSERT_TRUE(destination.HoldsDataUpTo(len));
        }
        /* The last 100,000 bytes, with no end, into a 1 MiB buffer */
        {
            const size_t start = s_objectSize - 100000;
            GuardedBuffer destination(1024 * 1024);
            String range = String("bytes=") + std::to_string(start).c_str() + "-";
            auto request = s_NewRequest(allocator, env, "GET", range.c_str());
            S3::S3MetaRequestResult result = {};
            ASSERT_SUCCESS(
                s_Run(env, S3::S3GetObjectMetaRequestOptions::Create(request, destination.buf), false, result));
            ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, result.errorCode);
            ASSERT_UINT_EQUALS(100000, destination.buf.len);
            ASSERT_TRUE(destination.HoldsDataUpTo(100000));
        }

        ASSERT_SUCCESS(s_CleanUpEnv(allocator, env));
    }
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(S3BufferRange, s_TestS3BufferRange)

/* A buffer too small for the object fails with AWS_ERROR_SHORT_BUFFER and leaves len at 0. */
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

        ASSERT_SUCCESS(s_Upload(allocator, env, Vector<uint8_t>(s_objectSize, s_data)));

        GuardedBuffer destination(1024 * 1024);
        auto request = s_NewRequest(allocator, env, "GET", nullptr);
        S3::S3MetaRequestResult result = {};
        ASSERT_SUCCESS(s_Run(env, S3::S3GetObjectMetaRequestOptions::Create(request, destination.buf), false, result));
        ASSERT_INT_EQUALS(AWS_ERROR_SHORT_BUFFER, result.errorCode);
        ASSERT_UINT_EQUALS(0, destination.buf.len);

        ASSERT_SUCCESS(s_CleanUpEnv(allocator, env));
    }
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(S3BufferTooSmall, s_TestS3BufferTooSmall)

/* No network: a buffer download that's cancelled right away. The options are destroyed right after MakeMetaRequest,
 * the FinishCallback reports an error, len stays 0, nothing is written to the buffer, and the request and the client
 * both shut down. */
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
        auto options = S3::S3GetObjectMetaRequestOptions::Create(request, destination.buf);
        ASSERT_NOT_NULL(options.get());
        std::promise<int> finished;
        std::promise<void> shutdown;
        options->SetFinishCallback([&finished](const S3::S3MetaRequestResult &result)
                                   { finished.set_value(result.errorCode); });
        options->SetShutdownCallback([&shutdown]() { shutdown.set_value(); });

        auto metaRequest = client->MakeMetaRequest(*options);
        ASSERT_NOT_NULL(metaRequest.get());
        metaRequest->Cancel();
        options.reset();

        auto finishedFuture = finished.get_future();
        ASSERT_TRUE(finishedFuture.wait_for(std::chrono::seconds(60)) == std::future_status::ready);
        ASSERT_FALSE(finishedFuture.get() == AWS_ERROR_SUCCESS);
        ASSERT_UINT_EQUALS(0, destination.buf.len);

        metaRequest.reset();
        ASSERT_TRUE(shutdown.get_future().wait_for(std::chrono::seconds(30)) == std::future_status::ready);
        ASSERT_TRUE(destination.HoldsDataUpTo(0));

        client.reset();
        ASSERT_TRUE(clientShutdown.get_future().wait_for(std::chrono::seconds(30)) == std::future_status::ready);
    }
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(S3BufferDownloadCancelled, s_TestS3BufferDownloadCancelled)
