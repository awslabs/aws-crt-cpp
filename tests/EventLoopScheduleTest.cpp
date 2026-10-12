/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/io/Bootstrap.h>
#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/io/HostResolver.h>
#include <aws/testing/aws_test_harness.h>

#include <atomic>
#include <chrono>
#include <future>
#include <thread>

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
        Aws::Crt::Io::ScheduledTask scheduledTask;

        {
            Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
            Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
            Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);

            scheduledTask = clientBootstrap.GetNextEventLoop().Schedule(
                [&taskStatus](Aws::Crt::Io::TaskStatus status) { taskStatus.set_value(status); },
                std::chrono::seconds(30));
        }

        ASSERT_TRUE(std::future_status::ready == taskStatusFuture.wait_for(std::chrono::seconds(10)));
        ASSERT_TRUE(Aws::Crt::Io::TaskStatus::Canceled == taskStatusFuture.get());

        scheduledTask.Cancel();
    }

    return AWS_OP_SUCCESS;
}

static int s_TestEventLoopScheduleCancelBeforeFire(struct aws_allocator *allocator, void *ctx)
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
        auto scheduledTask = clientBootstrap.GetNextEventLoop().Schedule(
            [&taskStatus](Aws::Crt::Io::TaskStatus status) { taskStatus.set_value(status); }, std::chrono::seconds(30));

        scheduledTask.Cancel();

        ASSERT_TRUE(std::future_status::ready == taskStatusFuture.wait_for(std::chrono::seconds(10)));
        ASSERT_TRUE(Aws::Crt::Io::TaskStatus::Canceled == taskStatusFuture.get());

        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - scheduledAt);
        ASSERT_TRUE(elapsed.count() < 10);
    }

    return AWS_OP_SUCCESS;
}

static int s_TestEventLoopScheduleCancelAfterFireIsNoop(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        Aws::Crt::ApiHandle apiHandle(allocator);
        Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
        Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);

        std::atomic<int> invocations{0};
        std::promise<Aws::Crt::Io::TaskStatus> taskStatus;
        std::future<Aws::Crt::Io::TaskStatus> taskStatusFuture = taskStatus.get_future();

        auto scheduledTask = clientBootstrap.GetNextEventLoop().Schedule(
            [&](Aws::Crt::Io::TaskStatus status)
            {
                ++invocations;
                taskStatus.set_value(status);
            },
            std::chrono::milliseconds(10));

        ASSERT_TRUE(std::future_status::ready == taskStatusFuture.wait_for(std::chrono::seconds(10)));
        ASSERT_TRUE(Aws::Crt::Io::TaskStatus::RunReady == taskStatusFuture.get());

        scheduledTask.Cancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        ASSERT_INT_EQUALS(1, invocations.load());
    }

    return AWS_OP_SUCCESS;
}

static int s_TestEventLoopScheduleCancelFromWithinTask(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        Aws::Crt::ApiHandle apiHandle(allocator);
        Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
        Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);

        std::atomic<int> invocations{0};
        std::promise<Aws::Crt::Io::ScheduledTask> handle;
        std::shared_future<Aws::Crt::Io::ScheduledTask> handleFuture = handle.get_future().share();
        std::promise<Aws::Crt::Io::TaskStatus> taskStatus;
        std::future<Aws::Crt::Io::TaskStatus> taskStatusFuture = taskStatus.get_future();

        handle.set_value(clientBootstrap.GetNextEventLoop().Schedule(
            [&, handleFuture](Aws::Crt::Io::TaskStatus status)
            {
                ++invocations;
                Aws::Crt::Io::ScheduledTask self = handleFuture.get();
                self.Cancel();
                taskStatus.set_value(status);
            },
            std::chrono::milliseconds(50)));

        ASSERT_TRUE(std::future_status::ready == taskStatusFuture.wait_for(std::chrono::seconds(10)));
        ASSERT_TRUE(Aws::Crt::Io::TaskStatus::RunReady == taskStatusFuture.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        ASSERT_INT_EQUALS(1, invocations.load());
    }

    return AWS_OP_SUCCESS;
}

static int s_TestEventLoopScheduleDoubleCancel(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        Aws::Crt::ApiHandle apiHandle(allocator);
        Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
        Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);

        std::atomic<int> invocations{0};
        std::promise<Aws::Crt::Io::TaskStatus> taskStatus;
        std::future<Aws::Crt::Io::TaskStatus> taskStatusFuture = taskStatus.get_future();

        auto scheduledTask = clientBootstrap.GetNextEventLoop().Schedule(
            [&](Aws::Crt::Io::TaskStatus status)
            {
                ++invocations;
                taskStatus.set_value(status);
            },
            std::chrono::seconds(30));

        scheduledTask.Cancel();
        scheduledTask.Cancel();

        ASSERT_TRUE(std::future_status::ready == taskStatusFuture.wait_for(std::chrono::seconds(10)));
        ASSERT_TRUE(Aws::Crt::Io::TaskStatus::Canceled == taskStatusFuture.get());
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        ASSERT_INT_EQUALS(1, invocations.load());
    }

    return AWS_OP_SUCCESS;
}

static int s_TestEventLoopScheduleDefaultHandleCancelIsNoop(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        Aws::Crt::ApiHandle apiHandle(allocator);

        Aws::Crt::Io::ScheduledTask scheduledTask;
        scheduledTask.Cancel();
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(EventLoopScheduleRunsAfterDelay, s_TestEventLoopScheduleRunsAfterDelay)
AWS_TEST_CASE(EventLoopScheduleCanceledOnShutdown, s_TestEventLoopScheduleCanceledOnShutdown)
AWS_TEST_CASE(EventLoopScheduleCancelBeforeFire, s_TestEventLoopScheduleCancelBeforeFire)
AWS_TEST_CASE(EventLoopScheduleCancelAfterFireIsNoop, s_TestEventLoopScheduleCancelAfterFireIsNoop)
AWS_TEST_CASE(EventLoopScheduleCancelFromWithinTask, s_TestEventLoopScheduleCancelFromWithinTask)
AWS_TEST_CASE(EventLoopScheduleDoubleCancel, s_TestEventLoopScheduleDoubleCancel)
AWS_TEST_CASE(EventLoopScheduleDefaultHandleCancelIsNoop, s_TestEventLoopScheduleDefaultHandleCancelIsNoop)
