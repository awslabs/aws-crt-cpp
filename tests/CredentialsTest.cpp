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

#include <aws/auth/credentials.h>
#include <aws/testing/aws_test_harness.h>

#include <condition_variable>
#include <mutex>

using namespace Aws::Crt;
using namespace Aws::Crt::Auth;

static const char *s_access_key_id = "AccessKey";
static const char *s_secret_access_key = "Sekrit";
static const char *s_session_token = "Token";

class GetCredentialsWaiter
{
  public:
    GetCredentialsWaiter(std::shared_ptr<ICredentialsProvider> provider)
        : m_lock(), m_signal(), m_done(false), m_credentials(nullptr), m_provider(provider)
    {
    }

    void OnCreds(std::shared_ptr<Credentials> credentials)
    {
        std::unique_lock<std::mutex> lock(m_lock);
        m_done = true;
        m_credentials = credentials;
        m_signal.notify_one();
    }

    std::shared_ptr<Credentials> GetCredentials()
    {
        {
            std::unique_lock<std::mutex> lock(m_lock);
            m_done = false;
            m_credentials = nullptr;
        }

        m_provider->GetCredentials([this](std::shared_ptr<Credentials> credentials) { OnCreds(credentials); });

        {
            std::unique_lock<std::mutex> lock(m_lock);
            m_signal.wait(lock, [this]() { return m_done == true; });

            return m_credentials;
        }
    }

  private:
    std::mutex m_lock;
    std::condition_variable m_signal;
    bool m_done;
    std::shared_ptr<Credentials> m_credentials;
    std::shared_ptr<ICredentialsProvider> m_provider;
};

static int s_TestProviderStaticGet(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);

    {
        CredentialsProviderStaticConfig config;
        config.AccessKeyId = aws_byte_cursor_from_c_str(s_access_key_id);
        config.SecretAccessKey = aws_byte_cursor_from_c_str(s_secret_access_key);
        config.SessionToken = aws_byte_cursor_from_c_str(s_session_token);

        auto provider = CredentialsProvider::CreateCredentialsProviderStatic(config, allocator);
        GetCredentialsWaiter waiter(provider);

        auto creds = waiter.GetCredentials();
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(TestProviderStaticGet, s_TestProviderStaticGet)

static int s_TestProviderEnvironmentGet(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);

    {
        auto provider = CredentialsProvider::CreateCredentialsProviderEnvironment(allocator);
        GetCredentialsWaiter waiter(provider);

        auto creds = waiter.GetCredentials();
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(TestProviderEnvironmentGet, s_TestProviderEnvironmentGet)

static int s_TestProviderProfileGet(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);

    {
        CredentialsProviderProfileConfig config;

        auto provider = CredentialsProvider::CreateCredentialsProviderProfile(config, allocator);

        if (provider)
        {
            GetCredentialsWaiter waiter(provider);

            auto creds = waiter.GetCredentials();
        }
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(TestProviderProfileGet, s_TestProviderProfileGet)

static int s_TestProviderImdsGet(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);

    {
        apiHandle.InitializeLogging(Aws::Crt::LogLevel::Trace, stderr);

        Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
        ASSERT_TRUE(eventLoopGroup);

        Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
        ASSERT_TRUE(defaultHostResolver);

        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
        ASSERT_TRUE(clientBootstrap);
        clientBootstrap.EnableBlockingShutdown();

        CredentialsProviderImdsConfig config;
        config.Bootstrap = &clientBootstrap;

        auto provider = CredentialsProvider::CreateCredentialsProviderImds(config, allocator);
        GetCredentialsWaiter waiter(provider);

        auto creds = waiter.GetCredentials();
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(TestProviderImdsGet, s_TestProviderImdsGet)

static int s_TestProviderDefaultChainGet(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);

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

        auto provider = CredentialsProvider::CreateCredentialsProviderChainDefault(config, allocator);
        GetCredentialsWaiter waiter(provider);

        auto creds = waiter.GetCredentials();
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(TestProviderDefaultChainGet, s_TestProviderDefaultChainGet)