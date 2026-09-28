/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/io/EventLoopGroup.h>

#include <aws/common/task_scheduler.h>

#include <atomic>
#include <iostream>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            EventLoopGroup::EventLoopGroup(uint16_t threadCount, Allocator *allocator) noexcept
                : m_eventLoopGroup(nullptr), m_lastError(AWS_ERROR_SUCCESS)
            {
                m_eventLoopGroup = aws_event_loop_group_new_default(allocator, threadCount, NULL);
                if (m_eventLoopGroup == nullptr)
                {
                    m_lastError = aws_last_error();
                }
            }

            EventLoopGroup::EventLoopGroup(uint16_t cpuGroup, uint16_t threadCount, Allocator *allocator) noexcept
                : m_eventLoopGroup(nullptr), m_lastError(AWS_ERROR_SUCCESS)
            {
                m_eventLoopGroup =
                    aws_event_loop_group_new_default_pinned_to_cpu_group(allocator, threadCount, cpuGroup, NULL);
                if (m_eventLoopGroup == nullptr)
                {
                    m_lastError = aws_last_error();
                }
            }

            EventLoopGroup::~EventLoopGroup()
            {
                aws_event_loop_group_release(m_eventLoopGroup);
            }

            EventLoopGroup::EventLoopGroup(EventLoopGroup &&toMove) noexcept
                : m_eventLoopGroup(toMove.m_eventLoopGroup), m_lastError(toMove.m_lastError)
            {
                toMove.m_lastError = AWS_ERROR_UNKNOWN;
                toMove.m_eventLoopGroup = nullptr;
            }

            EventLoopGroup &EventLoopGroup::operator=(EventLoopGroup &&toMove) noexcept
            {
                m_eventLoopGroup = toMove.m_eventLoopGroup;
                m_lastError = toMove.m_lastError;
                toMove.m_lastError = AWS_ERROR_UNKNOWN;
                toMove.m_eventLoopGroup = nullptr;

                return *this;
            }

            int EventLoopGroup::LastError() const
            {
                return m_lastError;
            }

            EventLoopGroup::operator bool() const
            {
                return m_lastError == AWS_ERROR_SUCCESS;
            }

            aws_event_loop_group *EventLoopGroup::GetUnderlyingHandle() noexcept
            {
                if (*this)
                {
                    return m_eventLoopGroup;
                }

                return nullptr;
            }

            class ScheduledTask::Impl : public std::enable_shared_from_this<ScheduledTask::Impl>
            {
              public:
                Impl(aws_event_loop *loop, std::function<void(TaskStatus)> &&fn) : m_loop(loop), m_fn(std::move(fn))
                {
                    aws_task_init(&m_run.task, Impl::OnRun, this, "cpp-crt-event-loop-task");
                    aws_task_init(&m_cancel.task, Impl::OnCancel, this, "cpp-crt-event-loop-cancel-task");
                }

                void Schedule(std::chrono::nanoseconds run_in) noexcept
                {
                    m_run.self = shared_from_this();

                    uint64_t currentTimestamp = 0;
                    aws_event_loop_current_clock_time(m_loop, &currentTimestamp);
                    aws_event_loop_schedule_task_future(
                        m_loop, &m_run.task, currentTimestamp + static_cast<uint64_t>(run_in.count()));
                }

                void Cancel() noexcept
                {
                    if (m_done.load() || m_cancelRequested.exchange(true))
                    {
                        return;
                    }

                    m_cancel.self = shared_from_this();
                    aws_event_loop_schedule_task_now(m_loop, &m_cancel.task);
                }

              private:
                struct Queued
                {
                    aws_task task;
                    std::shared_ptr<Impl> self;
                };

                static void OnRun(struct aws_task *, void *arg, enum aws_task_status status)
                {
                    auto *self = reinterpret_cast<Impl *>(arg);
                    std::shared_ptr<Impl> runSelf = std::move(self->m_run.self);
                    std::function<void(TaskStatus)> fn;
                    fn.swap(self->m_fn);
                    self->m_done.store(true);
                    fn(static_cast<TaskStatus>(status));
                }

                static void OnCancel(struct aws_task *, void *arg, enum aws_task_status status)
                {
                    auto *self = reinterpret_cast<Impl *>(arg);
                    std::shared_ptr<Impl> cancelSelf = std::move(self->m_cancel.self);
                    if (status == AWS_TASK_STATUS_RUN_READY && !self->m_done.load())
                    {
                        aws_event_loop_cancel_task(self->m_loop, &self->m_run.task);
                    }
                }

                aws_event_loop *m_loop;
                std::function<void(TaskStatus)> m_fn;
                Queued m_run;
                Queued m_cancel;
                std::atomic<bool> m_cancelRequested{false};
                std::atomic<bool> m_done{false};
            };

            ScheduledTask::ScheduledTask(std::shared_ptr<Impl> impl) noexcept : m_impl(std::move(impl)) {}

            void ScheduledTask::Cancel() noexcept
            {
                if (m_impl)
                {
                    m_impl->Cancel();
                }
            }

            EventLoop::EventLoop(aws_event_loop *loop) noexcept : m_loop(loop) {}

            ScheduledTask EventLoop::Schedule(
                std::function<void(TaskStatus)> &&task,
                std::chrono::nanoseconds run_in) noexcept
            {
                auto impl = MakeShared<ScheduledTask::Impl>(ApiAllocator(), m_loop, std::move(task));
                impl->Schedule(run_in);
                return ScheduledTask(impl);
            }
        } // namespace Io

    } // namespace Crt
} // namespace Aws
