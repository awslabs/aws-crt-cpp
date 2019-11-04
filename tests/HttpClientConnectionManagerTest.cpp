/*
 * Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
#include <aws/crt/crypto/Hash.h>
#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/Uri.h>

#include <aws/testing/aws_test_harness.h>

#include <aws/io/logging.h>

#include <condition_variable>
#include <fstream>
#include <mutex>

using namespace Aws::Crt;

/* make 30 connections, release them to the pool, then make sure the destructor cleans everything up properly. */
static int s_TestHttpClientConnectionManagerResourceSafety(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();

    Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    ASSERT_TRUE(tlsContext);

    Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions = tlsContext.NewConnectionOptions();

    ByteCursor cursor = ByteCursorFromCString("https://s3.amazonaws.com");
    Io::Uri uri(cursor, allocator);

    auto hostName = uri.GetHostName();
    tlsConnectionOptions.SetServerName(hostName);

    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(10000);

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
    ASSERT_TRUE(eventLoopGroup);

    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
    ASSERT_TRUE(defaultHostResolver);

    Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
    ASSERT_TRUE(clientBootstrap);

    std::condition_variable semaphore;
    std::mutex semaphoreLock;
    size_t connectionCount = 0;
    size_t connectionsFailed = 0;
    size_t totalExpectedConnections = 30;

    Http::HttpClientConnectionOptions connectionOptions;
    connectionOptions.Bootstrap = &clientBootstrap;
    connectionOptions.SocketOptions = socketOptions;
    connectionOptions.TlsOptions = tlsConnectionOptions;
    connectionOptions.HostName = String((const char *)hostName.ptr, hostName.len);
    connectionOptions.Port = 443;

    Http::HttpClientConnectionManagerOptions connectionManagerOptions;
    connectionManagerOptions.ConnectionOptions = connectionOptions;
    connectionManagerOptions.MaxConnections = totalExpectedConnections;
    connectionManagerOptions.EnableBlockingShutdown = true;

    auto connectionManager =
        Http::HttpClientConnectionManager::NewClientConnectionManager(connectionManagerOptions, allocator);
    ASSERT_TRUE(connectionManager);

    Vector<std::shared_ptr<Http::HttpClientConnection>> connections;

    auto onConnectionAvailable = [&](std::shared_ptr<Http::HttpClientConnection> newConnection, int errorCode) {
        {
            std::lock_guard<std::mutex> lockGuard(semaphoreLock);

            if (!errorCode)
            {
                connections.push_back(newConnection);
                connectionCount++;
            }
            else
            {
                connectionsFailed++;
            }
        }
        semaphore.notify_one();
    };

    for (size_t i = 0; i < totalExpectedConnections; ++i)
    {
        ASSERT_TRUE(connectionManager->AcquireConnection(onConnectionAvailable));
    }
    {
        std::unique_lock<std::mutex> uniqueLock(semaphoreLock);
        semaphore.wait(uniqueLock, [&]() { return connectionCount + connectionsFailed == totalExpectedConnections; });
    }

    /* make sure the test was actually meaningful. */
    ASSERT_TRUE(connectionCount > 0);
    Vector<std::shared_ptr<Http::HttpClientConnection>> connectionsCpy = connections;
    connections.clear();

    /* this will trigger a mutation to connections, hence the copy. */
    connectionsCpy.clear();

    {
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);

        ASSERT_TRUE(connections.empty());
        connectionsCpy.clear();
    }

    /* now let everything tear down and make sure we don't leak or deadlock.*/
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(HttpClientConnectionManagerResourceSafety, s_TestHttpClientConnectionManagerResourceSafety)

static int s_TestHttpClientConnectionWithPendingAcquisitions(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();

    Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    ASSERT_TRUE(tlsContext);

    Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions = tlsContext.NewConnectionOptions();

    ByteCursor cursor = ByteCursorFromCString("https://s3.amazonaws.com");
    Io::Uri uri(cursor, allocator);

    auto hostName = uri.GetHostName();
    tlsConnectionOptions.SetServerName(hostName);

    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(10000);

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
    ASSERT_TRUE(eventLoopGroup);

    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
    ASSERT_TRUE(defaultHostResolver);

    Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
    ASSERT_TRUE(clientBootstrap);

    std::condition_variable semaphore;
    std::mutex semaphoreLock;
    size_t connectionsFailed = 0;
    size_t totalExpectedConnections = 30;

    Http::HttpClientConnectionOptions connectionOptions;
    connectionOptions.Bootstrap = &clientBootstrap;
    connectionOptions.SocketOptions = socketOptions;
    connectionOptions.TlsOptions = tlsConnectionOptions;
    connectionOptions.HostName = String((const char *)hostName.ptr, hostName.len);
    connectionOptions.Port = 443;

    Http::HttpClientConnectionManagerOptions connectionManagerOptions;
    connectionManagerOptions.ConnectionOptions = connectionOptions;
    connectionManagerOptions.MaxConnections = totalExpectedConnections / 2;
    connectionManagerOptions.EnableBlockingShutdown = true;

    auto connectionManager =
        Http::HttpClientConnectionManager::NewClientConnectionManager(connectionManagerOptions, allocator);
    ASSERT_TRUE(connectionManager);

    Vector<std::shared_ptr<Http::HttpClientConnection>> connections;

    auto onConnectionAvailable = [&](std::shared_ptr<Http::HttpClientConnection> newConnection, int errorCode) {
        {
            std::lock_guard<std::mutex> lockGuard(semaphoreLock);

            if (!errorCode)
            {
                connections.push_back(newConnection);
            }
            else
            {
                connectionsFailed++;
            }
        }
        semaphore.notify_one();
    };

    {
        for (size_t i = 0; i < totalExpectedConnections; ++i)
        {
            ASSERT_TRUE(connectionManager->AcquireConnection(onConnectionAvailable));
        }
        std::unique_lock<std::mutex> uniqueLock(semaphoreLock);
        semaphore.wait(uniqueLock, [&]() {
            return connections.size() + connectionsFailed >= connectionManagerOptions.MaxConnections;
        });
    }

    /* make sure the test was actually meaningful. */
    ASSERT_FALSE(connections.empty());

    Vector<std::shared_ptr<Http::HttpClientConnection>> connectionsCpy = connections;
    connections.clear();

    connectionsCpy.clear();
    {
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);
        /* release should have given us more connections. */
        ASSERT_FALSE(connections.empty());
        connectionsCpy = connections;
        connections.clear();
    }

    connectionsCpy.clear();

    {
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);
        connections.clear();
    }

    /* now let everything tear down and make sure we don't leak or deadlock.*/
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(HttpClientConnectionWithPendingAcquisitions, s_TestHttpClientConnectionWithPendingAcquisitions)

static int s_TestHttpClientConnectionWithPendingAcquisitionsAndClosedConnections(
    struct aws_allocator *allocator,
    void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);
    ByteCursor cursor = ByteCursorFromCString("https://s3.amazonaws.com");
    Io::Uri uri(cursor, allocator);

    Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();

    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(10000);
    Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    ASSERT_TRUE(tlsContext);

    Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions = tlsContext.NewConnectionOptions();

    auto hostName = uri.GetHostName();
    tlsConnectionOptions.SetServerName(hostName);

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
    ASSERT_TRUE(eventLoopGroup);

    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
    ASSERT_TRUE(defaultHostResolver);

    Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
    ASSERT_TRUE(clientBootstrap);

    std::condition_variable semaphore;
    std::mutex semaphoreLock;
    size_t connectionCount = 0;
    size_t connectionsFailed = 0;
    size_t totalExpectedConnections = 30;

    Http::HttpClientConnectionOptions connectionOptions;
    connectionOptions.Bootstrap = &clientBootstrap;
    connectionOptions.SocketOptions = socketOptions;
    connectionOptions.TlsOptions = tlsConnectionOptions;
    connectionOptions.HostName = String((const char *)hostName.ptr, hostName.len);
    connectionOptions.Port = 443;

    Http::HttpClientConnectionManagerOptions connectionManagerOptions;
    connectionManagerOptions.ConnectionOptions = connectionOptions;
    connectionManagerOptions.MaxConnections = totalExpectedConnections / 2;
    connectionManagerOptions.EnableBlockingShutdown = true;

    auto connectionManager =
        Http::HttpClientConnectionManager::NewClientConnectionManager(connectionManagerOptions, allocator);
    ASSERT_TRUE(connectionManager);

    Vector<std::shared_ptr<Http::HttpClientConnection>> connections;

    auto onConnectionAvailable = [&](std::shared_ptr<Http::HttpClientConnection> newConnection, int errorCode) {
        {
            std::lock_guard<std::mutex> lockGuard(semaphoreLock);

            if (!errorCode)
            {
                connections.push_back(newConnection);
                connectionCount++;
            }
            else
            {
                connectionsFailed++;
            }
        }
        semaphore.notify_one();
    };

    {
        for (size_t i = 0; i < totalExpectedConnections; ++i)
        {
            ASSERT_TRUE(connectionManager->AcquireConnection(onConnectionAvailable));
        }
        std::unique_lock<std::mutex> uniqueLock(semaphoreLock);
        semaphore.wait(uniqueLock, [&]() {
            return connectionCount + connectionsFailed == connectionManagerOptions.MaxConnections;
        });
    }

    /* make sure the test was actually meaningful. */
    ASSERT_TRUE(connectionCount > 0);

    Vector<std::shared_ptr<Http::HttpClientConnection>> connectionsCpy = connections;
    connections.clear();
    size_t i = 0;
    for (auto &connection : connectionsCpy)
    {
        if (i++ & 0x01 && connection->IsOpen())
        {
            connection->Close();
        }
        connection.reset();
    }
    std::unique_lock<std::mutex> uniqueLock(semaphoreLock);
    semaphore.wait(uniqueLock, [&]() { return (connectionCount + connectionsFailed == totalExpectedConnections); });

    /* release should have given us more connections. */
    ASSERT_FALSE(connections.empty());
    connections.clear();

    /* now let everything tear down and make sure we don't leak or deadlock.*/
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(
    HttpClientConnectionWithPendingAcquisitionsAndClosedConnections,
    s_TestHttpClientConnectionWithPendingAcquisitionsAndClosedConnections)

static int s_TestHttpClientConnectionManagerMonitoring(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);
    apiHandle.InitializeLogging(Aws::Crt::LogLevel::Debug, "./log.txt");

    ByteCursor cursor = ByteCursorFromCString("https://aws-crt-test-stuff.s3.amazonaws.com/http_test_doc.txt");
    Io::Uri uri(cursor, allocator);

    Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();

    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(10000);
    Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    ASSERT_TRUE(tlsContext);

    Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions = tlsContext.NewConnectionOptions();

    auto hostName = uri.GetHostName();
    tlsConnectionOptions.SetServerName(hostName);

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
    ASSERT_TRUE(eventLoopGroup);

    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
    ASSERT_TRUE(defaultHostResolver);

    Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
    ASSERT_TRUE(clientBootstrap);

    Aws::Crt::Http::HttpConnectionMonitoringOptions monitoringOptions;
    monitoringOptions.MinimumThroughputBytesPerSecond = 50000;
    monitoringOptions.MinimumThroughputFailureThresholdInSeconds = 3;

    Http::HttpClientConnectionOptions connectionOptions;
    connectionOptions.Bootstrap = &clientBootstrap;
    connectionOptions.SocketOptions = socketOptions;
    connectionOptions.TlsOptions = tlsConnectionOptions;
    connectionOptions.HostName = String((const char *)hostName.ptr, hostName.len);
    connectionOptions.Port = 443;
    connectionOptions.MonitoringOptions = monitoringOptions;

    Http::HttpClientConnectionManagerOptions connectionManagerOptions;
    connectionManagerOptions.ConnectionOptions = connectionOptions;
    connectionManagerOptions.MaxConnections = 1;
    connectionManagerOptions.EnableBlockingShutdown = true;

    auto connectionManager =
        Http::HttpClientConnectionManager::NewClientConnectionManager(connectionManagerOptions, allocator);
    ASSERT_TRUE(connectionManager);

    std::shared_ptr<Http::HttpClientConnection> connection(nullptr);
    bool errorOccured = true;
    bool connectionShutdown = false;
    int onCompletedErrorCode = 0;

    std::condition_variable semaphore;
    std::mutex semaphoreLock;

    auto onConnectionAvailable = [&](std::shared_ptr<Http::HttpClientConnection> newConnection, int errorCode) {
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);

        if (!errorCode)
        {
            connection = newConnection;
            errorOccured = false;
        }
        else
        {
            connectionShutdown = true;
        }

        semaphore.notify_one();
    };

    std::unique_lock<std::mutex> semaphoreULock(semaphoreLock);
    ASSERT_TRUE(connectionManager->AcquireConnection(onConnectionAvailable));
    semaphore.wait(semaphoreULock, [&]() { return connection || connectionShutdown; });

    ASSERT_FALSE(errorOccured);
    ASSERT_FALSE(connectionShutdown);
    ASSERT_TRUE(connection);

    int responseCode = 0;
    std::ofstream downloadedFile("http_download_test_file.txt", std::ios_base::binary);
    ASSERT_TRUE(downloadedFile);

    Http::HttpRequest request;
    Http::HttpRequestOptions requestOptions;
    requestOptions.request = &request;

    bool streamCompleted = false;
    requestOptions.onStreamComplete = [&](Http::HttpStream &stream, int errorCode) {
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);

        streamCompleted = true;
        onCompletedErrorCode = errorCode;
        if (errorCode)
        {
            errorOccured = true;
        }

        semaphore.notify_one();
    };
    requestOptions.onIncomingHeadersBlockDone = nullptr;
    requestOptions.onIncomingHeaders =
        [&](Http::HttpStream &stream, enum aws_http_header_block, const Http::HttpHeader *header, std::size_t len) {
            responseCode = stream.GetResponseStatusCode();
        };
    requestOptions.onIncomingBody = [&](Http::HttpStream &, const ByteCursor &data) {
        AWS_LOGF_DEBUG(AWS_LS_IO_CHANNEL, "Read body callback - %zu bytes", data.len);
        std::this_thread::sleep_for(std::chrono::seconds(1));
        downloadedFile.write((const char *)data.ptr, data.len);
    };

    request.SetMethod(ByteCursorFromCString("GET"));
    request.SetPath(uri.GetPathAndQuery());

    Http::HttpHeader host_header;
    host_header.name = ByteCursorFromCString("host");
    host_header.value = uri.GetHostName();
    request.AddHeader(host_header);

    connection->NewClientStream(requestOptions);
    semaphore.wait(semaphoreULock, [&]() { return streamCompleted; });
    ASSERT_INT_EQUALS(200, responseCode);

    ASSERT_INT_EQUALS(onCompletedErrorCode, (int)AWS_ERROR_HTTP_CHANNEL_THROUGHPUT_FAILURE);
    ASSERT_TRUE(errorOccured);

    connection = nullptr;

    downloadedFile.flush();
    downloadedFile.close();

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(TestHttpClientConnectionManagerMonitoring, s_TestHttpClientConnectionManagerMonitoring)