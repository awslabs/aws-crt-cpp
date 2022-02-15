/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Types.h>
#include <aws/crt/io/EventLoopGroup.h>
#include <iostream>
#include <mutex>

#if __cplusplus >= 201103L // If using C++ 11
#    include <thread>
#endif

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            // Static variables
            EventLoopGroup *EventLoopGroup::s_static_event_loop_group = nullptr;
            std::mutex EventLoopGroup::s_lock;
#if __cplusplus >= 201103L // If using C++ 11
            int EventLoopGroup::s_static_event_loop_group_threads = std::thread::hardware_concurrency();
#else // If not using C++ 11, default to 1
            int EventLoopGroup::s_static_event_loop_group_threads = 1;
#endif

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

            EventLoopGroup::~EventLoopGroup() { aws_event_loop_group_release(m_eventLoopGroup); }

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

            int EventLoopGroup::LastError() const { return m_lastError; }

            EventLoopGroup::operator bool() const { return m_lastError == AWS_ERROR_SUCCESS; }

            aws_event_loop_group *EventLoopGroup::GetUnderlyingHandle() noexcept
            {
                if (*this)
                {
                    return m_eventLoopGroup;
                }

                return nullptr;
            }

            void EventLoopGroup::ReleaseStaticDefault()
            {
                std::lock_guard<std::mutex> lock(s_lock);
                if (s_static_event_loop_group != nullptr)
                {
                    Aws::Crt::Delete(s_static_event_loop_group, g_allocator);
                    s_static_event_loop_group = nullptr;
                }
            }
            EventLoopGroup &EventLoopGroup::GetOrCreateStaticDefault()
            {
                std::lock_guard<std::mutex> lock(s_lock);
                if (s_static_event_loop_group == nullptr)
                {
                    s_static_event_loop_group =
                        Aws::Crt::New<EventLoopGroup>(g_allocator, s_static_event_loop_group_threads);
                }
                return *s_static_event_loop_group;
            }

        } // namespace Io

    } // namespace Crt
} // namespace Aws
