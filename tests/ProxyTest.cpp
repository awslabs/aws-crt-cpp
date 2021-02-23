/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/common/environment.h>

#include <aws/crt/Api.h>
#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/Uri.h>

#include <aws/testing/aws_test_harness.h>

#include <condition_variable>
#include <mutex>

using namespace Aws::Crt;

using namespace Aws::Crt::Io;
using namespace Aws::Crt::Http;

struct HttpConnectionManagerProxyTestState
{
    HttpConnectionManagerProxyTestState() : m_streamComplete(false), m_streamStatusCode(0) {}

    std::condition_variable m_signal;
    std::mutex m_lock;

    bool m_streamComplete;
    int32_t m_streamStatusCode;
    Aws::Crt::StringStream m_responseBuffer;

    HttpClientConnectionProxyOptions m_proxyOptions;
    std::shared_ptr<TlsContext> m_tlsContext;
    std::shared_ptr<TlsContext> m_proxyTlsContext;
    std::shared_ptr<EventLoopGroup> m_eventLoopGroup;
    std::shared_ptr<DefaultHostResolver> m_hostResolver;
    std::shared_ptr<ClientBootstrap> m_clientBootstrap;
    std::shared_ptr<HttpClientConnectionManager> m_connectionManager;
    std::shared_ptr<HttpClientConnection> m_connection;
    std::shared_ptr<HttpRequest> m_request;
    std::shared_ptr<HttpClientStream> m_stream;
};

AWS_STATIC_STRING_FROM_LITERAL(s_https_endpoint, "https://s3.amazonaws.com");
AWS_STATIC_STRING_FROM_LITERAL(s_http_endpoint, "http://www.example.com");

static void s_InitializeProxiedConnectionManager(
    struct aws_allocator *allocator,
    HttpConnectionManagerProxyTestState &testState,
    struct aws_byte_cursor url)
{
    testState.m_eventLoopGroup = Aws::Crt::MakeShared<EventLoopGroup>(allocator, 1, allocator);
    testState.m_hostResolver =
        Aws::Crt::MakeShared<DefaultHostResolver>(allocator, *testState.m_eventLoopGroup, 8, 30, allocator);
    testState.m_clientBootstrap = Aws::Crt::MakeShared<ClientBootstrap>(
        allocator, *testState.m_eventLoopGroup, *testState.m_hostResolver, allocator);

    Io::Uri uri(url, allocator);
    auto hostName = uri.GetHostName();
    auto scheme = uri.GetScheme();
    bool useTls = true;
    if (aws_byte_cursor_eq_c_str_ignore_case(&scheme, "http"))
    {
        useTls = false;
    }

    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(10000);

    Http::HttpClientConnectionOptions connectionOptions;
    connectionOptions.Bootstrap = testState.m_clientBootstrap.get();
    connectionOptions.SocketOptions = socketOptions;
    connectionOptions.HostName = String((const char *)hostName.ptr, hostName.len);
    connectionOptions.Port = useTls ? 443 : 80;
    connectionOptions.ProxyOptions = testState.m_proxyOptions;

    if (useTls)
    {
        Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions;
        Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();
        testState.m_tlsContext =
            Aws::Crt::MakeShared<TlsContext>(allocator, tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);

        tlsConnectionOptions = testState.m_tlsContext->NewConnectionOptions();
        tlsConnectionOptions.SetServerName(hostName);
        connectionOptions.TlsOptions = tlsConnectionOptions;
    }

    Http::HttpClientConnectionManagerOptions connectionManagerOptions;
    connectionManagerOptions.ConnectionOptions = connectionOptions;

    testState.m_connectionManager =
        Http::HttpClientConnectionManager::NewClientConnectionManager(connectionManagerOptions, allocator);
}

static void s_AcquireProxyTestHttpConnection(HttpConnectionManagerProxyTestState &testState)
{

    int acquisitionErrorCode = 0;
    std::shared_ptr<Http::HttpClientConnection> connection;

    auto onConnectionAvailable = [&](std::shared_ptr<Http::HttpClientConnection> newConnection, int errorCode) {
        {
            std::lock_guard<std::mutex> lockGuard(testState.m_lock);

            acquisitionErrorCode = errorCode;
            if (!errorCode)
            {
                connection = newConnection;
            }
        }
        testState.m_signal.notify_one();
    };

    testState.m_connectionManager->AcquireConnection(onConnectionAvailable);
    std::unique_lock<std::mutex> uniqueLock(testState.m_lock);
    testState.m_signal.wait(uniqueLock, [&]() { return connection != nullptr || acquisitionErrorCode != 0; });

    testState.m_connection = connection;
}

AWS_STATIC_STRING_FROM_LITERAL(s_httpProxyHostEnvVariable, "AWS_TEST_HTTP_PROXY_HOST");
AWS_STATIC_STRING_FROM_LITERAL(s_httpProxyPortEnvVariable, "AWS_TEST_HTTP_PROXY_PORT");
AWS_STATIC_STRING_FROM_LITERAL(s_httpsProxyHostEnvVariable, "AWS_TEST_HTTPS_PROXY_HOST");
AWS_STATIC_STRING_FROM_LITERAL(s_httpsProxyPortEnvVariable, "AWS_TEST_HTTPS_PROXY_PORT");
AWS_STATIC_STRING_FROM_LITERAL(s_httpProxyBasicHostEnvVariable, "AWS_TEST_HTTP_PROXY_BASIC_HOST");
AWS_STATIC_STRING_FROM_LITERAL(s_httpProxyBasicPortEnvVariable, "AWS_TEST_HTTP_PROXY_BASIC_PORT");

enum HttpProxyTestHostType
{
    Http,
    Https,
    HttpBasic,
};

static const struct aws_string *s_GetProxyHostVariable(enum HttpProxyTestHostType proxyHostType)
{
    switch (proxyHostType)
    {
        case HttpProxyTestHostType::Http:
            return s_httpProxyHostEnvVariable;

        case HttpProxyTestHostType::Https:
            return s_httpsProxyHostEnvVariable;

        case HttpProxyTestHostType::HttpBasic:
            return s_httpProxyBasicHostEnvVariable;

        default:
            return NULL;
    }
}

static const struct aws_string *s_GetProxyPortVariable(enum HttpProxyTestHostType proxyHostType)
{
    switch (proxyHostType)
    {
        case HttpProxyTestHostType::Http:
            return s_httpProxyPortEnvVariable;

        case HttpProxyTestHostType::Https:
            return s_httpsProxyPortEnvVariable;

        case HttpProxyTestHostType::HttpBasic:
            return s_httpProxyBasicPortEnvVariable;

        default:
            return NULL;
    }
}

static int s_InitializeProxyEnvironmentalOptions(
    HttpClientConnectionProxyOptions &proxyOptions,
    struct aws_allocator *allocator,
    enum HttpProxyTestHostType proxyHostType)
{
    struct aws_string *proxy_host_name = NULL;
    struct aws_string *proxy_port = NULL;

    ASSERT_SUCCESS(aws_get_environment_value(allocator, s_GetProxyHostVariable(proxyHostType), &proxy_host_name));
    ASSERT_SUCCESS(aws_get_environment_value(allocator, s_GetProxyPortVariable(proxyHostType), &proxy_port));

    proxyOptions.HostName = Aws::Crt::String((const char *)proxy_host_name->bytes);
    proxyOptions.Port = atoi((const char *)proxy_port->bytes);

    aws_string_destroy(proxy_host_name);
    aws_string_destroy(proxy_port);

    return AWS_OP_SUCCESS;
}

static int s_TestConnectionManagerTunnelingProxyHttp(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        Aws::Crt::ApiHandle apiHandle(allocator);

        HttpConnectionManagerProxyTestState testState;
        s_InitializeProxyEnvironmentalOptions(testState.m_proxyOptions, allocator, HttpProxyTestHostType::Http);
        testState.m_proxyOptions.ProxyConnectionType = AwsHttpProxyConnectionType::Tunneling;

        s_InitializeProxiedConnectionManager(allocator, testState, aws_byte_cursor_from_string(s_https_endpoint));

        s_AcquireProxyTestHttpConnection(testState);
        ASSERT_TRUE(testState.m_connection != nullptr);
    }

    /* now let everything tear down and make sure we don't leak or deadlock.*/
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(ConnectionManagerTunnelingProxyHttp, s_TestConnectionManagerTunnelingProxyHttp)

static void s_InitializeTlsToProxy(HttpConnectionManagerProxyTestState &testState, struct aws_allocator *allocator)
{
    Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions;
    Aws::Crt::Io::TlsContextOptions proxyTlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();
    proxyTlsCtxOptions.SetVerifyPeer(false);

    testState.m_proxyTlsContext =
        Aws::Crt::MakeShared<TlsContext>(allocator, proxyTlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);

    tlsConnectionOptions = testState.m_proxyTlsContext->NewConnectionOptions();
    ByteCursor proxyName = ByteCursorFromString(testState.m_proxyOptions.HostName);
    tlsConnectionOptions.SetServerName(proxyName);

    testState.m_proxyOptions.TlsOptions = tlsConnectionOptions;
}

static int s_TestConnectionManagerTunnelingProxyHttps(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        Aws::Crt::ApiHandle apiHandle(allocator);

        HttpConnectionManagerProxyTestState testState;
        s_InitializeProxyEnvironmentalOptions(testState.m_proxyOptions, allocator, HttpProxyTestHostType::Https);
        testState.m_proxyOptions.ProxyConnectionType = AwsHttpProxyConnectionType::Tunneling;

        s_InitializeTlsToProxy(testState, allocator);

        s_InitializeProxiedConnectionManager(allocator, testState, aws_byte_cursor_from_string(s_https_endpoint));

        s_AcquireProxyTestHttpConnection(testState);
        ASSERT_TRUE(testState.m_connection != nullptr);
    }

    /* now let everything tear down and make sure we don't leak or deadlock.*/
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(ConnectionManagerTunnelingProxyHttps, s_TestConnectionManagerTunnelingProxyHttps)

static void s_MakeForwardingTestRequest(HttpConnectionManagerProxyTestState &testState, struct aws_allocator *allocator)
{
    testState.m_request = Aws::Crt::MakeShared<HttpRequest>(allocator, allocator);
    testState.m_request->SetMethod(ByteCursorFromCString("GET"));
    testState.m_request->SetPath(ByteCursorFromCString("/"));

    HttpRequestOptions requestOptions;
    requestOptions.request = testState.m_request.get();

    requestOptions.onIncomingBody = [&testState](Http::HttpStream &, const ByteCursor &data) {
        std::lock_guard<std::mutex> lock(testState.m_lock);

        Aws::Crt::String dataString((const char *)data.ptr, data.len);
        testState.m_responseBuffer << dataString;
    };

    requestOptions.onIncomingHeaders =
        [&testState](Http::HttpStream &, enum aws_http_header_block, const Http::HttpHeader *, std::size_t) {
            std::lock_guard<std::mutex> lock(testState.m_lock);
            if (testState.m_streamStatusCode == 0)
            {
                testState.m_streamStatusCode = testState.m_stream->GetResponseStatusCode();
            }
        };

    requestOptions.onStreamComplete = [&testState](Http::HttpStream &stream, int errorCode) {
        {
            std::lock_guard<std::mutex> lock(testState.m_lock);
            testState.m_streamComplete = true;
        }
        testState.m_signal.notify_one();
    };

    testState.m_stream = testState.m_connection->NewClientStream(requestOptions);
    testState.m_stream->Activate();
}

static void s_WaitOnTestStream(HttpConnectionManagerProxyTestState &testState)
{
    std::unique_lock<std::mutex> uniqueLock(testState.m_lock);
    testState.m_signal.wait(
        uniqueLock, [&testState]() { return testState.m_streamComplete || testState.m_stream == nullptr; });
}

static int s_TestConnectionManagerForwardingProxy(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        Aws::Crt::ApiHandle apiHandle(allocator);

        HttpConnectionManagerProxyTestState testState;
        s_InitializeProxyEnvironmentalOptions(testState.m_proxyOptions, allocator, HttpProxyTestHostType::Http);
        testState.m_proxyOptions.ProxyConnectionType = AwsHttpProxyConnectionType::Forwarding;

        s_InitializeProxiedConnectionManager(allocator, testState, aws_byte_cursor_from_string(s_http_endpoint));

        s_AcquireProxyTestHttpConnection(testState);
        ASSERT_TRUE(testState.m_connection != nullptr);

        s_MakeForwardingTestRequest(testState, allocator);

        s_WaitOnTestStream(testState);

        ASSERT_TRUE(testState.m_streamStatusCode == 200);
        Aws::Crt::String response = testState.m_responseBuffer.str();

        ASSERT_TRUE(response.find("example") != Aws::Crt::String::npos);
    }

    /* now let everything tear down and make sure we don't leak or deadlock.*/
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(ConnectionManagerForwardingProxy, s_TestConnectionManagerForwardingProxy)
