/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/s3/S3.h>

#include <aws/testing/aws_test_harness.h>

#include <chrono>
#include <future>

using namespace Aws::Crt;

/* A meta request's ShutdownCallback must fire once it finishes and the caller drops it, and the client must then
 * shut down. The request is cancelled right away, so no network or credentials are needed. */
static int s_TestS3MetaRequestShutdownCallbackFires(Allocator *allocator, void *)
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

        auto options = S3::S3GetObjectMetaRequestOptions::Create(request, [](ByteCursor, uint64_t) { return true; });
        ASSERT_NOT_NULL(options.get());
        std::promise<int> finished;
        std::promise<void> shutdown;
        options->SetFinishCallback([&finished](const S3::S3MetaRequestResult &result)
                                   { finished.set_value(result.errorCode); });
        options->SetShutdownCallback([&shutdown]() { shutdown.set_value(); });

        auto metaRequest = client->MakeMetaRequest(*options);
        ASSERT_NOT_NULL(metaRequest.get());
        metaRequest->Cancel();

        auto finishedFuture = finished.get_future();
        ASSERT_TRUE(finishedFuture.wait_for(std::chrono::seconds(60)) == std::future_status::ready);
        ASSERT_FALSE(finishedFuture.get() == AWS_ERROR_SUCCESS);

        metaRequest.reset();
        options.reset();
        ASSERT_TRUE(
            shutdown.get_future().wait_for(std::chrono::seconds(30)) == std::future_status::ready,
            "ShutdownCallback never fired");

        client.reset();
        ASSERT_TRUE(
            clientShutdown.get_future().wait_for(std::chrono::seconds(30)) == std::future_status::ready,
            "client never shut down");
    }
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(S3MetaRequestShutdownCallbackFires, s_TestS3MetaRequestShutdownCallbackFires)
