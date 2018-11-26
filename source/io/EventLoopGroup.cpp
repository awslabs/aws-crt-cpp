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
#include <aws/crt/io/EventLoopGroup.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            EventLoopGroup::EventLoopGroup(Allocator* allocator) noexcept : EventLoopGroup(0, allocator)
            {                
            }


            EventLoopGroup::EventLoopGroup( uint16_t threadCount, Allocator* allocator) noexcept:
                m_lastError(AWS_ERROR_SUCCESS)
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


            EventLoopGroup::EventLoopGroup(EventLoopGroup&& toMove) noexcept:
                m_eventLoopGroup(toMove.m_eventLoopGroup),
                m_lastError(toMove.m_lastError)
            {
                toMove.m_lastError = AWS_ERROR_UNKNOWN;
                AWS_ZERO_STRUCT(toMove.m_eventLoopGroup);
            }

            EventLoopGroup& EventLoopGroup::operator =(EventLoopGroup&& toMove) noexcept
            {
                m_eventLoopGroup = toMove.m_eventLoopGroup;
                m_lastError = toMove.m_lastError;
                toMove.m_lastError = AWS_ERROR_UNKNOWN;
                AWS_ZERO_STRUCT(toMove.m_eventLoopGroup);
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
                    return &m_eventLoopGroup;
                }

                return nullptr;
            }

        }

    }
}
