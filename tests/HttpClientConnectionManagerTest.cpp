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


/*
static int s_StressTestHttpConnectionManager(
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

ASSERT_FALSE(connections.empty());
connections.clear();

return AWS_OP_SUCCESS;
}
*/

static const char *s_stressTestHost = "s3.amazonaws.com";

struct TestCrtObjectSet {
    std::shared_ptr<Aws::Crt::Io::TlsContext> tlsContext;
    std::shared_ptr<Aws::Crt::Io::DefaultHostResolver> resolver;
    std::shared_ptr<Aws::Crt::Io::ClientBootstrap> bootstrap;
    std::shared_ptr<Aws::Crt::Http::HttpClientConnectionManager> manager;
};

static void s_BuildStressTestManagerSet(struct aws_allocator *allocator, Aws::Crt::Vector<TestCrtObjectSet> &managers,
    Aws::Crt::Io::EventLoopGroup &eventLoopGroup, size_t maxConnections, const char *host, size_t count) {

    for (size_t i = 0; i < count; ++i) {
        TestCrtObjectSet objectSet;

        objectSet.resolver = Aws::Crt::MakeShared<Aws::Crt::Io::DefaultHostResolver>(allocator, eventLoopGroup, 8, 30,
                                                                                     allocator);
        objectSet.bootstrap = Aws::Crt::MakeShared<Aws::Crt::Io::ClientBootstrap>(allocator, eventLoopGroup,
                                                                                  *objectSet.resolver);

        Aws::Crt::Io::SocketOptions socketOptions;
        socketOptions.SetConnectTimeoutMs(10000);

        Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();
        objectSet.tlsContext = Aws::Crt::MakeShared<Aws::Crt::Io::TlsContext>(allocator, tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);

        Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions = objectSet.tlsContext->NewConnectionOptions();

        auto hostCursor = aws_byte_cursor_from_c_str(host);
        tlsConnectionOptions.SetServerName(hostCursor);

        Http::HttpClientConnectionOptions connectionOptions;
        connectionOptions.Bootstrap = objectSet.bootstrap.get();
        connectionOptions.SocketOptions = socketOptions;
        connectionOptions.TlsOptions = tlsConnectionOptions;
        connectionOptions.HostName = host;
        connectionOptions.Port = 443;

        Http::HttpClientConnectionManagerOptions connectionManagerOptions;
        connectionManagerOptions.ConnectionOptions = connectionOptions;
        connectionManagerOptions.MaxConnections = maxConnections;

        objectSet.manager =
            Http::HttpClientConnectionManager::NewClientConnectionManager(connectionManagerOptions, allocator);

        managers.push_back(objectSet);
    }
}

static int s_DoStressTestInstance(struct aws_allocator *allocator, size_t elgSize, size_t maxConnection, size_t requestCount, size_t threadCount, size_t shareClient) {
    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(elgSize, allocator);
    ASSERT_TRUE(eventLoopGroup);

    Aws::Crt::Vector<TestCrtObjectSet> managers;
    s_BuildStressTestManagerSet(allocator, managers, eventLoopGroup, maxConnection, s_stressTestHost, shareClient ? 1 : threadCount);
}

static void s_DoStressTestIteration(struct aws_allocator *allocator) {
    static size_t elgSizes[] = { 1, 4 };
    static size_t maxConnections[] = { 1, 10, 100 };
    static size_t requestCounts[] = {1, 100, 250};
    static size_t threadCounts[] = {1, 2, 8};
    static bool shareClients[] = {false, true};

    for (size_t elgSizeIndex = 0; elgSizeIndex < AWS_ARRAY_SIZE(elgSizes); ++elgSizeIndex) {
        size_t elgSize = elgSizes[elgSizeIndex];
        for (size_t maxConnectionsIndex = 0; maxConnectionsIndex < AWS_ARRAY_SIZE(maxConnections); ++maxConnectionsIndex) {
            size_t maxConnection = maxConnections[maxConnectionsIndex];
            for (size_t requestCountsIndex = 0; requestCountsIndex < AWS_ARRAY_SIZE(requestCounts); ++requestCountsIndex) {
                size_t requestCount = requestCounts[requestCountsIndex];
                for (size_t threadCountsIndex = 0; threadCountsIndex < AWS_ARRAY_SIZE(threadCounts); ++threadCountsIndex) {
                    size_t threadCount = threadCounts[threadCountsIndex];
                    for (size_t shareClientIndex = 0; shareClientIndex < AWS_ARRAY_SIZE(shareClients); ++shareClientIndex) {
                        bool shareClient = shareClients[shareClientIndex];

                        s_DoStressTestInstance(allocator, elgSize, maxConnection, requestCount, threadCount, shareClient);
                    }
                }
            }
        }
    }
}

static int s_StressTestHttpConnectionManager(struct aws_allocator *allocator, void *ctx) {
    (void) ctx;
    bool done = false;
    while (!done) {
        s_DoStressTestIteration(allocator);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(StressTestHttpConnectionManager, s_StressTestHttpConnectionManager)