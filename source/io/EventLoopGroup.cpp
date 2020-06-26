/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/io/EventLoopGroup.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            EventLoopGroup::EventLoopGroup(uint16_t threadCount, Allocator *allocator) noexcept
                : m_lastError(AWS_ERROR_SUCCESS)
            {
                AWS_ZERO_STRUCT(m_eventLoopGroup);

                if (aws_event_loop_group_default_init(&m_eventLoopGroup, allocator, threadCount))
                {
                    m_lastError = aws_last_error();
                }
            }

            EventLoopGroup::~EventLoopGroup()
            {
                if (!m_lastError)
                {
                    aws_event_loop_group_clean_up(&m_eventLoopGroup);
                    m_lastError = AWS_ERROR_SUCCESS;
                }
                AWS_ZERO_STRUCT(m_eventLoopGroup);
            }

            EventLoopGroup::EventLoopGroup(EventLoopGroup &&toMove) noexcept
                : m_eventLoopGroup(toMove.m_eventLoopGroup), m_lastError(toMove.m_lastError)
            {
                toMove.m_lastError = AWS_ERROR_UNKNOWN;
                AWS_ZERO_STRUCT(toMove.m_eventLoopGroup);
            }

            EventLoopGroup &EventLoopGroup::operator=(EventLoopGroup &&toMove) noexcept
            {
                m_eventLoopGroup = toMove.m_eventLoopGroup;
                m_lastError = toMove.m_lastError;
                toMove.m_lastError = AWS_ERROR_UNKNOWN;
                AWS_ZERO_STRUCT(toMove.m_eventLoopGroup);
                return *this;
            }

            int EventLoopGroup::LastError() const { return m_lastError; }

            EventLoopGroup::operator bool() const { return m_lastError == AWS_ERROR_SUCCESS; }

            aws_event_loop_group *EventLoopGroup::GetUnderlyingHandle() noexcept
            {
                if (*this)
                {
                    return &m_eventLoopGroup;
                }

                return nullptr;
            }

        } // namespace Io

    } // namespace Crt
} // namespace Aws
