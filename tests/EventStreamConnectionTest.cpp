/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>

#include <aws/crt/event-stream/EventStream.h>

#include <aws/testing/aws_test_harness.h>

#include <sstream>
#include <iostream>

using namespace Aws::Crt;

static int s_TestEventStreamConnect(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        ApiHandle apiHandle(allocator);
        Io::TlsContextOptions tlsCtxOptions = Io::TlsContextOptions::InitDefaultClient();
        Io::TlsContext tlsContext(tlsCtxOptions, Io::TlsMode::CLIENT, allocator);
        ASSERT_TRUE(tlsContext);

        Io::TlsConnectionOptions tlsConnectionOptions = tlsContext.NewConnectionOptions();
        Io::SocketOptions socketOptions;
        socketOptions.SetConnectTimeoutMs(1000);

        Io::EventLoopGroup eventLoopGroup(0, allocator);
        ASSERT_TRUE(eventLoopGroup);

        Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
        ASSERT_TRUE(defaultHostResolver);

        Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
        ASSERT_TRUE(clientBootstrap);
        clientBootstrap.EnableBlockingShutdown();
        auto messageAmender = [&](void) -> Eventstream::MessageAmendment {
            Aws::Crt::List<Eventstream::EventStreamHeader> authHeaders;
            authHeaders.push_back(Eventstream::EventStreamHeader(String("client-name"), String("accepted.testy_mc_testerson"), g_allocator));
            Eventstream::MessageAmendment messageAmendInfo(authHeaders);
            return messageAmendInfo;
        };
        std::shared_ptr<Eventstream::EventstreamRpcConnection> connection(nullptr);

        auto onConnect = [&](const std::shared_ptr<Eventstream::EventstreamRpcConnection> &newConnection) {
            connection = newConnection;
            std::cout << "Connected" << std::endl;
        };

        auto onDisconnect = [&](const std::shared_ptr<Eventstream::EventstreamRpcConnection> &newConnection,
                                int errorCode) 
        {
            std::cout << "Disconnected" << std::endl;
        };

        String hostName = "127.0.0.1";
        Eventstream::EventstreamRpcConnectionOptions options;
        options.Bootstrap = &clientBootstrap;
        options.SocketOptions = socketOptions;
        options.HostName = hostName;
        options.Port = 8033;
        options.ConnectMessageAmenderCallback = messageAmender;
        options.OnConnectCallback = onConnect;
        options.OnDisconnectCallback = onDisconnect;
        options.OnErrorCallback = nullptr;
        options.OnPingCallback = nullptr;

        ASSERT_TRUE(Eventstream::EventstreamRpcConnection::CreateConnection(options, allocator));
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(EventStreamConnect, s_TestEventStreamConnect)
