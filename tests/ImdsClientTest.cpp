/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/auth/aws_imds_client.h>
#include <aws/crt/Api.h>
#include <aws/crt/ImdsClient.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/testing/aws_test_harness.h>
#include <condition_variable>
#include <mutex>

using namespace Aws::Crt;
using namespace Aws::Crt::Auth;
using namespace Aws::Crt::Imds;

static int s_TestCreatingImdsClientFromRawClient(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);
    {
        aws_event_loop_group el_group;
        ASSERT_SUCCESS(aws_event_loop_group_default_init(&el_group, allocator, 1));
        aws_host_resolver resolver;
        aws_host_resolver_init_default(&resolver, allocator, 8, &el_group);

        aws_client_bootstrap_options bootstrap_options;
        AWS_ZERO_STRUCT(bootstrap_options);
        bootstrap_options.event_loop_group = &el_group;
        bootstrap_options.host_resolver = &resolver;
        aws_client_bootstrap *bootstrap = aws_client_bootstrap_new(allocator, &bootstrap_options);
        ASSERT_NOT_NULL(bootstrap);
        aws_imds_client_options options;
        AWS_ZERO_STRUCT(options);
        options.bootstrap = bootstrap;

        struct aws_imds_client *raw_client = aws_imds_client_new(allocator, &options);
        ImdsClient client(raw_client);
        aws_imds_client_release(raw_client);

        aws_client_bootstrap_release(bootstrap);
        aws_host_resolver_clean_up(&resolver);
        aws_event_loop_group_clean_up(&el_group);
    }
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(TestCreatingImdsClientFromRawClient, s_TestCreatingImdsClientFromRawClient);

static int s_TestCreatingImdsClient(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);
    {

        Aws::Crt::Io::EventLoopGroup eventLoopGroup(1, allocator);
        ASSERT_TRUE(eventLoopGroup);

        Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
        ASSERT_TRUE(defaultHostResolver);

        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
        ASSERT_TRUE(clientBootstrap);
        clientBootstrap.EnableBlockingShutdown();

        ImdsClientConfig config;
        config.Bootstrap = &clientBootstrap;
        ImdsClient client(config);
    }
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(TestCreatingImdsClient, s_TestCreatingImdsClient);

static int s_TestImdsClientGetInstanceInfo(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);
    {

        Aws::Crt::Io::EventLoopGroup eventLoopGroup(1, allocator);
        ASSERT_TRUE(eventLoopGroup);

        Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
        ASSERT_TRUE(defaultHostResolver);

        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
        ASSERT_TRUE(clientBootstrap);
        clientBootstrap.EnableBlockingShutdown();

        ImdsClientConfig config;
        config.Bootstrap = &clientBootstrap;
        ImdsClient client(config);

        std::condition_variable signal;
        std::mutex lock;
        int error = 0;
        InstanceInfo info;
        AWS_ZERO_STRUCT(info);

        auto callback = [&](const InstanceInfo &instanceInfo, int errorCode, void *) {
            std::unique_lock<std::mutex> ulock(lock);
            info = instanceInfo;
            error = errorCode;
            signal.notify_one();
        };

        client.GetInstanceInfo(callback, nullptr);

        {
            std::unique_lock<std::mutex> ulock(lock);
            signal.wait(ulock);
        }

        if (error == 0)
        {
            ASSERT_FALSE(0 == info.instanceId.len);
            ASSERT_NOT_NULL(info.instanceId.ptr);
        }
    }
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(TestImdsClientGetInstanceInfo, s_TestImdsClientGetInstanceInfo);

static int s_TestImdsClientGetCredentials(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);
    {

        Aws::Crt::Io::EventLoopGroup eventLoopGroup(1, allocator);
        ASSERT_TRUE(eventLoopGroup);

        Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
        ASSERT_TRUE(defaultHostResolver);

        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
        ASSERT_TRUE(clientBootstrap);
        clientBootstrap.EnableBlockingShutdown();

        ImdsClientConfig config;
        config.Bootstrap = &clientBootstrap;
        ImdsClient client(config);

        std::condition_variable signal;
        std::mutex lock;
        int error = 0;
        InstanceInfo info;
        AWS_ZERO_STRUCT(info);

        ByteBuf role;
        auto roleCallback = [&](const ByteBuf *resource, int errorCode, void *) {
            std::unique_lock<std::mutex> ulock(lock);
            role = ByteBufNewCopy(allocator, resource->buffer, resource->len);
            error = errorCode;
            signal.notify_one();
        };

        client.GetAttachedIamRole(roleCallback, nullptr);

        {
            std::unique_lock<std::mutex> ulock(lock);
            signal.wait(ulock);
        }

        std::shared_ptr<Auth::Credentials> creds(nullptr);
        auto callback = [&](const Auth::Credentials &credentials, int errorCode, void *) {
            std::unique_lock<std::mutex> ulock(lock);
            creds = Aws::Crt::MakeShared<Credentials>(allocator, credentials.GetUnderlyingHandle());
            error = errorCode;
            signal.notify_one();
        };

        client.GetCredentials(ByteCursorFromByteBuf(role), callback, nullptr);

        {
            std::unique_lock<std::mutex> ulock(lock);
            signal.wait(ulock);
        }

        if (error == 0 && creds)
        {
            ASSERT_FALSE(0 == creds->GetAccessKeyId().len);
            ASSERT_NOT_NULL(creds->GetAccessKeyId().ptr);

            ASSERT_FALSE(0 == creds->GetSecretAccessKey().len);
            ASSERT_NOT_NULL(creds->GetSecretAccessKey().ptr);

            ASSERT_FALSE(0 == creds->GetSessionToken().len);
            ASSERT_NOT_NULL(creds->GetSessionToken().ptr);
        }

        ByteBufDelete(role);
    }
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(TestImdsClientGetCredentials, s_TestImdsClientGetCredentials);
