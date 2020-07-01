/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/crypto/Hash.h>
#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/io/Uri.h>

#include <aws/testing/aws_test_harness.h>

#include <aws/io/logging.h>

#include <condition_variable>
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

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(1, allocator);
    ASSERT_TRUE(eventLoopGroup);

    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
    ASSERT_TRUE(defaultHostResolver);

    Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
    ASSERT_TRUE(clientBootstrap);
    clientBootstrap.EnableBlockingShutdown();

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
    {
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
            semaphore.wait(
                uniqueLock, [&]() { return connectionCount + connectionsFailed == totalExpectedConnections; });
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
    }
    connectionManager->InitiateShutdown().get();

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

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(1, allocator);
    ASSERT_TRUE(eventLoopGroup);

    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
    ASSERT_TRUE(defaultHostResolver);

    Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
    ASSERT_TRUE(clientBootstrap);
    clientBootstrap.EnableBlockingShutdown();

    std::condition_variable semaphore;
    std::mutex semaphoreLock;
    size_t connectionsFailed = 0;
    size_t connectionsAcquired = 0;
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
    {
        Vector<std::shared_ptr<Http::HttpClientConnection>> connections;

        auto onConnectionAvailable = [&](std::shared_ptr<Http::HttpClientConnection> newConnection, int errorCode) {
            {
                std::lock_guard<std::mutex> lockGuard(semaphoreLock);

                if (!errorCode)
                {
                    connections.push_back(newConnection);
                    connectionsAcquired++;
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
                return connections.size() + connectionsFailed == connectionManagerOptions.MaxConnections;
            });
        }

        /* make sure the test was actually meaningful. */
        Vector<std::shared_ptr<Http::HttpClientConnection>> connectionsCpy;
        {
            std::lock_guard<std::mutex> lockGuard(semaphoreLock);
            ASSERT_FALSE(connections.empty());

            connectionsCpy = connections;
            connections.clear();
        }

        connectionsCpy.clear();

        bool done = false;
        while (!done)
        {
            {
                std::lock_guard<std::mutex> lockGuard(semaphoreLock);
                done = connectionsAcquired + connectionsFailed == totalExpectedConnections;

                connectionsCpy = connections;
                connections.clear();
            }
            connectionsCpy.clear();
        }
    }
    connectionManager->InitiateShutdown().get();

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

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(1, allocator);
    ASSERT_TRUE(eventLoopGroup);

    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
    ASSERT_TRUE(defaultHostResolver);

    Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
    ASSERT_TRUE(clientBootstrap);
    clientBootstrap.EnableBlockingShutdown();

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

    {
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

        {
            std::unique_lock<std::mutex> uniqueLock(semaphoreLock);
            semaphore.wait(
                uniqueLock, [&]() { return (connectionCount + connectionsFailed == totalExpectedConnections); });

            /* release should have given us more connections. */
            ASSERT_FALSE(connections.empty());
        }

        bool done = false;
        while (!done)
        {
            {
                std::lock_guard<std::mutex> lockGuard(semaphoreLock);
                done = connectionCount + connectionsFailed == totalExpectedConnections;
            }
            connectionsCpy = connections;
            connections.clear();
            connectionsCpy.clear();
        }
    }
    connectionManager->InitiateShutdown().get();

    /* now let everything tear down and make sure we don't leak or deadlock.*/
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(
    HttpClientConnectionWithPendingAcquisitionsAndClosedConnections,
    s_TestHttpClientConnectionWithPendingAcquisitionsAndClosedConnections)
