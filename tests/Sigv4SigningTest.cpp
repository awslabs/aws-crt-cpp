/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>

#include <aws/crt/auth/Credentials.h>
#include <aws/crt/auth/Sigv4Signing.h>
#include <aws/crt/http/HttpRequestResponse.h>

#include <aws/auth/credentials.h>
#include <aws/http/request_response.h>

#include <aws/testing/aws_test_harness.h>

#include <condition_variable>
#include <mutex>

#include <aws/testing/aws_test_allocators.h>

using namespace Aws::Crt;
using namespace Aws::Crt::Auth;
using namespace Aws::Crt::Http;

class SignWaiter
{
  public:
    SignWaiter() : m_lock(), m_signal(), m_done(false) {}

    void OnSigningComplete(const std::shared_ptr<Aws::Crt::Http::HttpRequest> &request, int errorCode)
    {
        std::unique_lock<std::mutex> lock(m_lock);
        m_done = true;
        m_signal.notify_one();
    }

    void Wait()
    {
        {
            std::unique_lock<std::mutex> lock(m_lock);
            m_signal.wait(lock, [this]() { return m_done == true; });
        }
    }

  private:
    std::mutex m_lock;
    std::condition_variable m_signal;
    bool m_done;
};

static std::shared_ptr<HttpRequest> s_MakeDummyRequest(Allocator *allocator)
{
    auto request = MakeShared<HttpRequest>(allocator);

    request->SetMethod(aws_byte_cursor_from_c_str("GET"));
    request->SetPath(aws_byte_cursor_from_c_str("http://www.test.com/mctest"));

    HttpHeader header;
    header.name = aws_byte_cursor_from_c_str("Host");
    header.value = aws_byte_cursor_from_c_str("www.test.com");

    request->AddHeader(header);

    auto bodyStream = Aws::Crt::MakeShared<std::stringstream>(allocator, "Something");
    request->SetBody(bodyStream);

    return request;
}

static std::shared_ptr<Credentials> s_MakeDummyCredentials(Allocator *allocator)
{
    return Aws::Crt::MakeShared<Credentials>(
        allocator,
        aws_byte_cursor_from_c_str("access"),
        aws_byte_cursor_from_c_str("secret"),
        aws_byte_cursor_from_c_str("token"),
        UINT64_MAX);
}

static std::shared_ptr<CredentialsProvider> s_MakeAsyncStaticProvider(
    Allocator *allocator,
    const Aws::Crt::Io::ClientBootstrap &bootstrap)
{
    struct aws_credentials_provider_imds_options imds_options;
    AWS_ZERO_STRUCT(imds_options);
    imds_options.bootstrap = bootstrap.GetUnderlyingHandle();

    struct aws_credentials_provider *provider1 = aws_credentials_provider_new_imds(allocator, &imds_options);

    aws_credentials_provider_static_options static_options;
    AWS_ZERO_STRUCT(static_options);
    static_options.access_key_id = aws_byte_cursor_from_c_str("access");
    static_options.secret_access_key = aws_byte_cursor_from_c_str("secret");
    static_options.session_token = aws_byte_cursor_from_c_str("token");

    struct aws_credentials_provider *provider2 = aws_credentials_provider_new_static(allocator, &static_options);

    struct aws_credentials_provider *providers[2] = {provider1, provider2};

    aws_credentials_provider_chain_options options;
    AWS_ZERO_STRUCT(options);
    options.providers = providers;
    options.provider_count = 2;

    struct aws_credentials_provider *provider_chain = aws_credentials_provider_new_chain(allocator, &options);
    aws_credentials_provider_release(provider1);
    aws_credentials_provider_release(provider2);
    if (provider_chain == NULL)
    {
        return nullptr;
    }

    return Aws::Crt::MakeShared<CredentialsProvider>(allocator, provider_chain, allocator);
}

static int s_Sigv4SigningTestCreateDestroy(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
        ASSERT_TRUE(eventLoopGroup);

        Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
        ASSERT_TRUE(defaultHostResolver);

        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
        ASSERT_TRUE(clientBootstrap);
        clientBootstrap.EnableBlockingShutdown();

        CredentialsProviderChainDefaultConfig config;
        config.Bootstrap = &clientBootstrap;

        auto provider = Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(config);

        auto signer = Aws::Crt::MakeShared<Sigv4HttpRequestSigner>(allocator, allocator);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(Sigv4SigningTestCreateDestroy, s_Sigv4SigningTestCreateDestroy)

static int s_Sigv4SigningTestSimple(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
        ASSERT_TRUE(eventLoopGroup);

        Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
        ASSERT_TRUE(defaultHostResolver);

        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
        ASSERT_TRUE(clientBootstrap);
        clientBootstrap.EnableBlockingShutdown();

        CredentialsProviderChainDefaultConfig config;
        config.Bootstrap = &clientBootstrap;

        auto provider = s_MakeAsyncStaticProvider(allocator, clientBootstrap);

        auto signer = Aws::Crt::MakeShared<Sigv4HttpRequestSigner>(allocator, allocator);

        auto request = s_MakeDummyRequest(allocator);

        AwsSigningConfig signingConfig(allocator);
        signingConfig.SetSigningTimepoint(Aws::Crt::DateTime());
        signingConfig.SetRegion("test");
        signingConfig.SetService("service");
        signingConfig.SetCredentialsProvider(provider);

        SignWaiter waiter;

        signer->SignRequest(
            request, signingConfig, [&](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &request, int errorCode) {
                waiter.OnSigningComplete(request, errorCode);
            });
        waiter.Wait();
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(Sigv4SigningTestSimple, s_Sigv4SigningTestSimple)

static int s_Sigv4SigningTestCredentials(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
        ASSERT_TRUE(eventLoopGroup);

        Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
        ASSERT_TRUE(defaultHostResolver);

        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
        ASSERT_TRUE(clientBootstrap);
        clientBootstrap.EnableBlockingShutdown();

        CredentialsProviderChainDefaultConfig config;
        config.Bootstrap = &clientBootstrap;

        auto signer = Aws::Crt::MakeShared<Sigv4HttpRequestSigner>(allocator, allocator);

        auto request = s_MakeDummyRequest(allocator);

        AwsSigningConfig signingConfig(allocator);
        signingConfig.SetSigningTimepoint(Aws::Crt::DateTime());
        signingConfig.SetRegion("test");
        signingConfig.SetService("service");
        signingConfig.SetCredentials(s_MakeDummyCredentials(allocator));

        SignWaiter waiter;

        signer->SignRequest(
            request, signingConfig, [&](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &request, int errorCode) {
                waiter.OnSigningComplete(request, errorCode);
            });
        waiter.Wait();
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(Sigv4SigningTestCredentials, s_Sigv4SigningTestCredentials)