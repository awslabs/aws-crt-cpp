/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/auth/credentials.h>
#include <aws/crt/Api.h>
#include <aws/crt/DateTime.h>
#include <aws/crt/auth/Credentials.h>
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
        : m_lock(), m_signal(), m_done(false), m_credentials(nullptr), m_provider(provider),
          m_errorCode(AWS_ERROR_SUCCESS)
    {
    }

    void OnCreds(std::shared_ptr<Credentials> credentials, int error_code)
    {
        std::unique_lock<std::mutex> lock(m_lock);
        m_done = true;
        m_credentials = credentials;
        m_errorCode = error_code;
        m_signal.notify_one();
    }

    std::shared_ptr<Credentials> GetCredentials()
    {
        {
            std::unique_lock<std::mutex> lock(m_lock);
            m_done = false;
            m_credentials = nullptr;
        }

        m_provider->GetCredentials(
            [this](std::shared_ptr<Credentials> credentials, int error_code) { OnCreds(credentials, error_code); });

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
    int m_errorCode;
};

static int s_TestCredentialsConstruction(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);
        uint64_t expire = Aws::Crt::DateTime::Now().Millis() / 1000 + 3600;
        aws_credentials *raw_creds = aws_credentials_new(
            allocator,
            aws_byte_cursor_from_c_str(s_access_key_id),
            aws_byte_cursor_from_c_str(s_secret_access_key),
            aws_byte_cursor_from_c_str(s_session_token),
            expire);

        ASSERT_NOT_NULL(raw_creds);
        Credentials creds(raw_creds);
        ASSERT_PTR_EQUALS(raw_creds, creds.GetUnderlyingHandle());
        auto cursor = creds.GetAccessKeyId();
        ASSERT_TRUE(aws_byte_cursor_eq_c_str(&cursor, s_access_key_id));
        cursor = creds.GetSecretAccessKey();
        ASSERT_TRUE(aws_byte_cursor_eq_c_str(&cursor, s_secret_access_key));
        cursor = creds.GetSessionToken();
        ASSERT_TRUE(aws_byte_cursor_eq_c_str(&cursor, s_session_token));
        ASSERT_UINT_EQUALS(expire, creds.GetExpirationTimepointInSeconds());

        Credentials creds2(raw_creds);
        ASSERT_TRUE(raw_creds == creds2.GetUnderlyingHandle());

        // We can/should safely release the raw creds here, but remember creds still holds it by ref counting.
        aws_credentials_release(raw_creds);

        cursor = creds2.GetAccessKeyId();
        ASSERT_TRUE(aws_byte_cursor_eq_c_str(&cursor, s_access_key_id));
        cursor = creds2.GetSecretAccessKey();
        ASSERT_TRUE(aws_byte_cursor_eq_c_str(&cursor, s_secret_access_key));
        cursor = creds2.GetSessionToken();
        ASSERT_TRUE(aws_byte_cursor_eq_c_str(&cursor, s_session_token));
        ASSERT_UINT_EQUALS(expire, creds2.GetExpirationTimepointInSeconds());
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(TestCredentialsConstruction, s_TestCredentialsConstruction)

static int s_TestProviderStaticGet(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);

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
    {
        ApiHandle apiHandle(allocator);

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
    {
        ApiHandle apiHandle(allocator);

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

    {
        ApiHandle apiHandle(allocator);
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

static int s_TestProviderDefaultChainGet(struct aws_allocator *allocator, void *ctx, bool manual_tls)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);

        Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
        ASSERT_TRUE(eventLoopGroup);

        Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
        ASSERT_TRUE(defaultHostResolver);

        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
        ASSERT_TRUE(clientBootstrap);
        clientBootstrap.EnableBlockingShutdown();

        Aws::Crt::Io::TlsContextOptions tlsOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient(allocator);
        Aws::Crt::Io::TlsContext tlsContext(tlsOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);

        CredentialsProviderChainDefaultConfig config;
        config.Bootstrap = &clientBootstrap;
        /* TlsContext didn't used to be an option. So test with and without setting it. */
        config.TlsContext = manual_tls ? &tlsContext : nullptr;

        auto provider = CredentialsProvider::CreateCredentialsProviderChainDefault(config, allocator);
        GetCredentialsWaiter waiter(provider);

        auto creds = waiter.GetCredentials();
    }

    return AWS_OP_SUCCESS;
}

static int s_TestProviderDefaultChainAutoTlsContextGet(struct aws_allocator *allocator, void *ctx)
{
    return s_TestProviderDefaultChainGet(allocator, ctx, false /*manual_tls*/);
}
AWS_TEST_CASE(TestProviderDefaultChainGet, s_TestProviderDefaultChainAutoTlsContextGet)

static int s_TestProviderDefaultChainManualTlsContextGet(struct aws_allocator *allocator, void *ctx)
{
    return s_TestProviderDefaultChainGet(allocator, ctx, true /*manual_tls*/);
}
AWS_TEST_CASE(TestProviderDefaultChainManualTlsContextGet, s_TestProviderDefaultChainManualTlsContextGet)

static int s_TestProviderDelegateGet(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);

        auto delegateGetCredentials = [&allocator]() -> std::shared_ptr<Credentials> {
            Credentials credentials(
                aws_byte_cursor_from_c_str(s_access_key_id),
                aws_byte_cursor_from_c_str(s_secret_access_key),
                aws_byte_cursor_from_c_str(s_session_token),
                UINT32_MAX,
                allocator);
            return Aws::Crt::MakeShared<Auth::Credentials>(allocator, credentials.GetUnderlyingHandle());
        };

        CredentialsProviderDelegateConfig config;
        config.Handler = delegateGetCredentials;
        auto provider = CredentialsProvider::CreateCredentialsProviderDelegate(config, allocator);
        GetCredentialsWaiter waiter(provider);

        auto creds = waiter.GetCredentials();
        auto cursor = creds->GetAccessKeyId();
        // Don't use ASSERT_STR_EQUALS(), which could log actual credentials if test fails.
        ASSERT_TRUE(aws_byte_cursor_eq_c_str(&cursor, s_access_key_id));
        cursor = creds->GetSecretAccessKey();
        ASSERT_TRUE(aws_byte_cursor_eq_c_str(&cursor, s_secret_access_key));
        cursor = creds->GetSessionToken();
        ASSERT_TRUE(aws_byte_cursor_eq_c_str(&cursor, s_session_token));
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(TestProviderDelegateGet, s_TestProviderDelegateGet)
