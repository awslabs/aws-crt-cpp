/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/common/error.h>
#include <aws/common/string.h>
#include <aws/crt/Api.h>
#include <aws/crt/io/Socks5ProxyOptions.h>
#include <aws/crt/io/Uri.h>
#include <aws/testing/aws_test_harness.h>
#include <climits>

using namespace Aws::Crt;
using namespace Aws::Crt::Io;

static int s_TestSocks5ProxyOptionsCreateFromUriNoAuth(struct aws_allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    const char *proxyUri = "socks5://proxy.example.com:1081";
    Uri uri(aws_byte_cursor_from_c_str(proxyUri), allocator);
    ASSERT_TRUE(uri);

    auto options = Socks5ProxyOptions::CreateFromUri(uri, 5000 /* timeout ms */, allocator);
    ASSERT_TRUE(options.has_value());

    const aws_socks5_proxy_options *raw = options->GetUnderlyingHandle();
    ASSERT_NOT_NULL(raw);
    ASSERT_NOT_NULL(raw->host);
    ASSERT_STR_EQUALS("proxy.example.com", aws_string_c_str(raw->host));
    ASSERT_INT_EQUALS(1081, raw->port);
    ASSERT_UINT_EQUALS(5000, raw->connection_timeout_ms);
    ASSERT_TRUE(*options);
    ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, options->LastError());
    ASSERT_INT_EQUALS(static_cast<int>(AwsSocks5AuthMethod::None), static_cast<int>(options->GetAuthMethod()));
    ASSERT_TRUE(raw->username == NULL);
    ASSERT_TRUE(raw->password == NULL);
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5HostResolutionMode::Client), static_cast<int>(options->GetHostResolutionMode()));
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5HostResolutionMode::Client), static_cast<int>(options->GetResolutionMode()));

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Socks5ProxyOptionsCreateFromUriNoAuth, s_TestSocks5ProxyOptionsCreateFromUriNoAuth)

static int s_TestSocks5ProxyOptionsCreateFromUriAuth(struct aws_allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    const char *proxyUri = "socks5h://user:pass@proxy.example.com";
    Uri uri(aws_byte_cursor_from_c_str(proxyUri), allocator);
    ASSERT_TRUE(uri);

    auto options = Socks5ProxyOptions::CreateFromUri(uri, 0, allocator);
    ASSERT_TRUE(options.has_value());

    const aws_socks5_proxy_options *raw = options->GetUnderlyingHandle();
    ASSERT_NOT_NULL(raw);
    ASSERT_NOT_NULL(raw->host);
    ASSERT_STR_EQUALS("proxy.example.com", aws_string_c_str(raw->host));
    /* default port */
    ASSERT_INT_EQUALS(1080, raw->port);
    ASSERT_UINT_EQUALS(0, raw->connection_timeout_ms);
    ASSERT_NOT_NULL(raw->username);
    ASSERT_NOT_NULL(raw->password);
    ASSERT_STR_EQUALS("user", aws_string_c_str(raw->username));
    ASSERT_STR_EQUALS("pass", aws_string_c_str(raw->password));
    ASSERT_TRUE(*options);
    ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, options->LastError());
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5AuthMethod::UsernamePassword), static_cast<int>(options->GetAuthMethod()));
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5HostResolutionMode::Proxy), static_cast<int>(options->GetHostResolutionMode()));
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5HostResolutionMode::Proxy), static_cast<int>(options->GetResolutionMode()));

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Socks5ProxyOptionsCreateFromUriAuth, s_TestSocks5ProxyOptionsCreateFromUriAuth)

static int s_TestSocks5ProxyOptionsCreateFromUriInvalid(struct aws_allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    const char *proxyUri = "http://proxy.example.com:1080";
    Uri uri(aws_byte_cursor_from_c_str(proxyUri), allocator);
    ASSERT_TRUE(uri);

    auto options = Socks5ProxyOptions::CreateFromUri(uri, 1000, allocator);
    ASSERT_FALSE(options.has_value());
    ASSERT_INT_EQUALS(AWS_ERROR_INVALID_ARGUMENT, aws_last_error());

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Socks5ProxyOptionsCreateFromUriInvalid, s_TestSocks5ProxyOptionsCreateFromUriInvalid)

static int s_TestSocks5ProxyOptionsCtorDefaults(struct aws_allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Socks5ProxyOptions options("proxy.example.com");

    ASSERT_TRUE(options);
    ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, options.LastError());
    ASSERT_INT_EQUALS(
        static_cast<int>(Socks5ProxyOptions::DefaultProxyPort), static_cast<int>(options.GetPort()));
    ASSERT_INT_EQUALS(static_cast<int>(AwsSocks5AuthMethod::None), static_cast<int>(options.GetAuthMethod()));
    ASSERT_FALSE(options.GetUsername().has_value());
    ASSERT_FALSE(options.GetPassword().has_value());
    ASSERT_UINT_EQUALS(0, options.GetConnectionTimeoutMs());
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5HostResolutionMode::Proxy), static_cast<int>(options.GetResolutionMode()));

    const aws_socks5_proxy_options *raw = options.GetUnderlyingHandle();
    ASSERT_NOT_NULL(raw);
    ASSERT_NOT_NULL(raw->host);
    ASSERT_STR_EQUALS("proxy.example.com", aws_string_c_str(raw->host));
    ASSERT_INT_EQUALS(Socks5ProxyOptions::DefaultProxyPort, raw->port);
    ASSERT_TRUE(raw->username == NULL);
    ASSERT_TRUE(raw->password == NULL);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Socks5ProxyOptionsCtorDefaults, s_TestSocks5ProxyOptionsCtorDefaults)

static int s_TestSocks5ProxyOptionsIgnoreCredentialsWhenAuthNone(struct aws_allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Socks5ProxyAuthConfig authConfig = Socks5ProxyAuthConfig::CreateNone();
    Socks5ProxyOptions options(
        "proxy.example.com",
        1080,
        authConfig,
        1000,
        AwsSocks5HostResolutionMode::Proxy,
        allocator);

    ASSERT_TRUE(options);
    ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, options.LastError());
    ASSERT_INT_EQUALS(static_cast<int>(AwsSocks5AuthMethod::None), static_cast<int>(options.GetAuthMethod()));
    ASSERT_FALSE(options.GetUsername().has_value());
    ASSERT_FALSE(options.GetPassword().has_value());

    const aws_socks5_proxy_options *raw = options.GetUnderlyingHandle();
    ASSERT_NOT_NULL(raw);
    ASSERT_TRUE(raw->username == NULL);
    ASSERT_TRUE(raw->password == NULL);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Socks5ProxyOptionsIgnoreCredentialsWhenAuthNone, s_TestSocks5ProxyOptionsIgnoreCredentialsWhenAuthNone)

static int s_TestSocks5ProxyOptionsCopyAndMove(struct aws_allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Socks5ProxyAuthConfig authConfig = Socks5ProxyAuthConfig::CreateUsernamePassword("user", "pass");
    Socks5ProxyOptions original(
        "proxy.example.com",
        1080,
        authConfig,
        2500,
        AwsSocks5HostResolutionMode::Proxy,
        allocator);

    ASSERT_TRUE(original);
    ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, original.LastError());
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5AuthMethod::UsernamePassword), static_cast<int>(original.GetAuthMethod()));

    const aws_socks5_proxy_options *rawOriginal = original.GetUnderlyingHandle();
    ASSERT_NOT_NULL(rawOriginal);
    ASSERT_NOT_NULL(rawOriginal->username);
    ASSERT_NOT_NULL(rawOriginal->password);

    Socks5ProxyOptions copy(original);
    const aws_socks5_proxy_options *rawCopy = copy.GetUnderlyingHandle();
    ASSERT_NOT_NULL(rawCopy);
    ASSERT_NOT_NULL(rawCopy->username);
    ASSERT_NOT_NULL(rawCopy->password);
    ASSERT_TRUE(copy);
    ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, copy.LastError());
    /* Deep copy should allocate distinct aws_string instances. */
    ASSERT_TRUE(rawOriginal->username != rawCopy->username);
    ASSERT_TRUE(rawOriginal->password != rawCopy->password);
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5HostResolutionMode::Proxy), static_cast<int>(copy.GetHostResolutionMode()));

    original.SetHostResolutionMode(AwsSocks5HostResolutionMode::Client);
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5HostResolutionMode::Client), static_cast<int>(original.GetHostResolutionMode()));
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5HostResolutionMode::Client), static_cast<int>(original.GetResolutionMode()));
    /* Copy must remain unchanged. */
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5HostResolutionMode::Proxy), static_cast<int>(copy.GetHostResolutionMode()));
    ASSERT_INT_EQUALS(static_cast<int>(AwsSocks5HostResolutionMode::Proxy), static_cast<int>(copy.GetResolutionMode()));

    Socks5ProxyOptions moved(std::move(original));
    const aws_socks5_proxy_options *rawMoved = moved.GetUnderlyingHandle();
    ASSERT_NOT_NULL(rawMoved);
    ASSERT_NOT_NULL(rawMoved->host);
    ASSERT_STR_EQUALS("proxy.example.com", aws_string_c_str(rawMoved->host));
    ASSERT_INT_EQUALS(1080, rawMoved->port);
    ASSERT_TRUE(moved);
    ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, moved.LastError());

    const aws_socks5_proxy_options *rawOriginalAfterMove = original.GetUnderlyingHandle();
    ASSERT_NOT_NULL(rawOriginalAfterMove);
    ASSERT_TRUE(rawOriginalAfterMove->host == NULL);
    ASSERT_TRUE(rawOriginalAfterMove->username == NULL);
    ASSERT_TRUE(rawOriginalAfterMove->password == NULL);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Socks5ProxyOptionsCopyAndMove, s_TestSocks5ProxyOptionsCopyAndMove)

static int s_TestSocks5ProxyOptionsSetters(struct aws_allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Socks5ProxyOptions options;
    ASSERT_FALSE(options);
    ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, options.LastError());

    options.SetConnectionTimeoutMs(1234);
    ASSERT_UINT_EQUALS(1234, options.GetConnectionTimeoutMs());

    ASSERT_TRUE(options.SetProxyEndpoint("proxy.example.com", 1080));
    ASSERT_TRUE(options);
    ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, options.LastError());
    auto hostOpt = options.GetHost();
    ASSERT_TRUE(hostOpt.has_value());
    ASSERT_STR_EQUALS("proxy.example.com", hostOpt->c_str());
    ASSERT_INT_EQUALS(1080, options.GetPort());

    options.SetHostResolutionMode(AwsSocks5HostResolutionMode::Client);
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5HostResolutionMode::Client), static_cast<int>(options.GetResolutionMode()));

    ASSERT_TRUE(options.SetAuthCredentials("user", "pass"));
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5AuthMethod::UsernamePassword), static_cast<int>(options.GetAuthMethod()));
    auto usernameOpt = options.GetUsername();
    auto passwordOpt = options.GetPassword();
    ASSERT_TRUE(usernameOpt.has_value());
    ASSERT_TRUE(passwordOpt.has_value());
    ASSERT_STR_EQUALS("user", usernameOpt->c_str());
    ASSERT_STR_EQUALS("pass", passwordOpt->c_str());

    options.SetConnectionTimeoutMs(4321);
    ASSERT_UINT_EQUALS(4321, options.GetConnectionTimeoutMs());

    ASSERT_TRUE(options.SetProxyEndpoint("new.proxy.local", 1090));
    hostOpt = options.GetHost();
    ASSERT_TRUE(hostOpt.has_value());
    ASSERT_STR_EQUALS("new.proxy.local", hostOpt->c_str());
    ASSERT_INT_EQUALS(1090, options.GetPort());
    ASSERT_UINT_EQUALS(4321, options.GetConnectionTimeoutMs());
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5HostResolutionMode::Client), static_cast<int>(options.GetResolutionMode()));
    usernameOpt = options.GetUsername();
    passwordOpt = options.GetPassword();
    ASSERT_TRUE(usernameOpt.has_value());
    ASSERT_TRUE(passwordOpt.has_value());
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5AuthMethod::UsernamePassword), static_cast<int>(options.GetAuthMethod()));

    ASSERT_FALSE(options.SetAuthCredentials("user", ""));
    ASSERT_INT_EQUALS(AWS_ERROR_INVALID_ARGUMENT, options.LastError());
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5AuthMethod::UsernamePassword), static_cast<int>(options.GetAuthMethod()));

    ASSERT_FALSE(options.SetProxyEndpoint("", 1090));
    ASSERT_INT_EQUALS(AWS_ERROR_INVALID_ARGUMENT, options.LastError());
    hostOpt = options.GetHost();
    ASSERT_TRUE(hostOpt.has_value());
    ASSERT_STR_EQUALS("new.proxy.local", hostOpt->c_str());
    ASSERT_INT_EQUALS(1090, options.GetPort());

    ASSERT_FALSE(options.SetProxyEndpoint("overflow.example.com", static_cast<uint32_t>(UINT16_MAX) + 1u));
    ASSERT_INT_EQUALS(AWS_ERROR_INVALID_ARGUMENT, options.LastError());
    hostOpt = options.GetHost();
    ASSERT_TRUE(hostOpt.has_value());
    ASSERT_STR_EQUALS("new.proxy.local", hostOpt->c_str());
    ASSERT_INT_EQUALS(1090, options.GetPort());

    options.ClearAuthCredentials();
    ASSERT_INT_EQUALS(static_cast<int>(AwsSocks5AuthMethod::None), static_cast<int>(options.GetAuthMethod()));
    ASSERT_FALSE(options.GetUsername().has_value());
    ASSERT_FALSE(options.GetPassword().has_value());
    ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, options.LastError());

    ASSERT_TRUE(options.SetProxyEndpoint("noauth.proxy.local", 1105));
    ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, options.LastError());
    auto hostOptAfterClear = options.GetHost();
    ASSERT_TRUE(hostOptAfterClear.has_value());
    ASSERT_INT_EQUALS(static_cast<int>(strlen("noauth.proxy.local")), static_cast<int>(hostOptAfterClear->length()));
    ASSERT_INT_EQUALS(1105, options.GetPort());
    ASSERT_INT_EQUALS(static_cast<int>(AwsSocks5AuthMethod::None), static_cast<int>(options.GetAuthMethod()));
    ASSERT_FALSE(options.GetUsername().has_value());
    ASSERT_FALSE(options.GetPassword().has_value());
    const aws_socks5_proxy_options *rawAfterClear = options.GetUnderlyingHandle();
    ASSERT_NOT_NULL(rawAfterClear);
    ASSERT_NOT_NULL(rawAfterClear->host);
    ASSERT_INT_EQUALS(strlen("noauth.proxy.local"), rawAfterClear->host->len);
    ASSERT_BIN_ARRAYS_EQUALS(
        "noauth.proxy.local", strlen("noauth.proxy.local"), rawAfterClear->host->bytes, rawAfterClear->host->len);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Socks5ProxyOptionsSetters, s_TestSocks5ProxyOptionsSetters)

static int s_TestSocks5ProxyOptionsAuthConfig(struct aws_allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Socks5ProxyOptions options;
    ASSERT_TRUE(options.SetProxyEndpoint("auth.proxy.local", 1085));
    ASSERT_TRUE(options);

    auto usernamePasswordConfig = Socks5ProxyAuthConfig::CreateUsernamePassword("userA", "passA");
    ASSERT_TRUE(options.SetAuth(usernamePasswordConfig));
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5AuthMethod::UsernamePassword), static_cast<int>(options.GetAuthMethod()));
    auto usernameOpt = options.GetUsername();
    auto passwordOpt = options.GetPassword();
    ASSERT_TRUE(usernameOpt.has_value());
    ASSERT_TRUE(passwordOpt.has_value());
    ASSERT_STR_EQUALS("userA", usernameOpt->c_str());
    ASSERT_STR_EQUALS("passA", passwordOpt->c_str());

    Socks5ProxyAuthConfig invalidNoneConfig;
    invalidNoneConfig.Method = AwsSocks5AuthMethod::None;
    invalidNoneConfig.Username = String("should-fail");
    ASSERT_FALSE(options.SetAuth(invalidNoneConfig));
    ASSERT_INT_EQUALS(AWS_ERROR_INVALID_ARGUMENT, options.LastError());
    ASSERT_INT_EQUALS(
        static_cast<int>(AwsSocks5AuthMethod::UsernamePassword), static_cast<int>(options.GetAuthMethod()));

    Socks5ProxyAuthConfig clearedConfig = Socks5ProxyAuthConfig::CreateNone();
    ASSERT_TRUE(options.SetAuth(clearedConfig));
    ASSERT_INT_EQUALS(static_cast<int>(AwsSocks5AuthMethod::None), static_cast<int>(options.GetAuthMethod()));
    ASSERT_FALSE(options.GetUsername().has_value());
    ASSERT_FALSE(options.GetPassword().has_value());
    ASSERT_INT_EQUALS(AWS_ERROR_SUCCESS, options.LastError());

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Socks5ProxyOptionsAuthConfig, s_TestSocks5ProxyOptionsAuthConfig)
