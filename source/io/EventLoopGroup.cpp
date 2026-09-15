/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/io/EventLoopGroup.h>

#include <aws/common/task_scheduler.h>

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

            namespace
            {
                class EventLoopTask
                {
                  public:
                    EventLoopTask(Allocator *allocator, std::function<void(TaskStatus)> &&fn)
                        : m_allocator(allocator), m_fn(std::move(fn))
                    {
                        aws_task_init(&m_task, EventLoopTask::OnTaskRun, this, "cpp-crt-event-loop-task");
                    }

                    aws_task *GetTask() noexcept { return &m_task; }

                  private:
                    static void OnTaskRun(struct aws_task *, void *arg, enum aws_task_status status)
                    {
                        auto *self = reinterpret_cast<EventLoopTask *>(arg);
                        self->m_fn(static_cast<TaskStatus>(status));
                        Delete(self, self->m_allocator);
                    }

                    aws_task m_task;
                    Allocator *m_allocator;
                    std::function<void(TaskStatus)> m_fn;
                };
            } // namespace

            EventLoop::EventLoop(aws_event_loop *loop) noexcept : m_loop(loop) {}

            void EventLoop::Schedule(std::function<void(TaskStatus)> &&task, std::chrono::nanoseconds run_in) noexcept
            {
                Allocator *allocator = ApiAllocator();
                auto *loopTask = New<EventLoopTask>(allocator, allocator, std::move(task));

                uint64_t currentTimestamp = 0;
                aws_event_loop_current_clock_time(m_loop, &currentTimestamp);
                aws_event_loop_schedule_task_future(
                    m_loop, loopTask->GetTask(), currentTimestamp + static_cast<uint64_t>(run_in.count()));
            }
        } // namespace Io

    } // namespace Crt
} // namespace Aws
