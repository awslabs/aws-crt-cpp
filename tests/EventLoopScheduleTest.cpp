/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/io/Bootstrap.h>
#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/io/HostResolver.h>
#include <aws/testing/aws_test_harness.h>

#include <chrono>
#include <future>

static int s_TestEventLoopScheduleRunsAfterDelay(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        Aws::Crt::ApiHandle apiHandle(allocator);
        Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
        Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);

        std::promise<Aws::Crt::Io::TaskStatus> taskStatus;
        std::future<Aws::Crt::Io::TaskStatus> taskStatusFuture = taskStatus.get_future();

        const auto scheduledAt = std::chrono::steady_clock::now();
        std::chrono::steady_clock::time_point ranAt;

        clientBootstrap.GetNextEventLoop().Schedule(
            [&](Aws::Crt::Io::TaskStatus status)
            {
                ranAt = std::chrono::steady_clock::now();
                taskStatus.set_value(status);
            },
            std::chrono::milliseconds(200));

        ASSERT_TRUE(std::future_status::ready == taskStatusFuture.wait_for(std::chrono::seconds(10)));
        ASSERT_TRUE(Aws::Crt::Io::TaskStatus::RunReady == taskStatusFuture.get());

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(ranAt - scheduledAt);
        ASSERT_TRUE(elapsed.count() >= 150);
    }

    return AWS_OP_SUCCESS;
}

static int s_TestEventLoopScheduleCanceledOnShutdown(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        Aws::Crt::ApiHandle apiHandle(allocator);

        std::promise<Aws::Crt::Io::TaskStatus> taskStatus;
        std::future<Aws::Crt::Io::TaskStatus> taskStatusFuture = taskStatus.get_future();

        {
            Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
            Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
            Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);

            clientBootstrap.GetNextEventLoop().Schedule(
                [&taskStatus](Aws::Crt::Io::TaskStatus status) { taskStatus.set_value(status); },
                std::chrono::seconds(30));
        }

        ASSERT_TRUE(std::future_status::ready == taskStatusFuture.wait_for(std::chrono::seconds(10)));
        ASSERT_TRUE(Aws::Crt::Io::TaskStatus::Canceled == taskStatusFuture.get());
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(EventLoopScheduleRunsAfterDelay, s_TestEventLoopScheduleRunsAfterDelay)
AWS_TEST_CASE(EventLoopScheduleCanceledOnShutdown, s_TestEventLoopScheduleCanceledOnShutdown)
