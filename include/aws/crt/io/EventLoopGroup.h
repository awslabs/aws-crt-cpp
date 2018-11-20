#pragma once
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
#include <aws/crt/Types.h>

#include <aws/io/event_loop.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            /**
             * A collection of event loops, this is used by all APIs that need to
             * do any IO. The number of threads used depends on your use-case. IF you
             * have a maximum of less than a few hundred connections 1 thread is the ideal
             * threadCount.
             *
             * There should only be one instance of an EventLoopGroup per application and it
             * should be passed to all network clients. One exception to this is if you 
             * want to peg different types of IO to different threads. In that case, you 
             * may want to have one event loop group dedicated to one IO activity and another
             * dedicated to another type.
             */
            class AWS_CRT_CPP_API EventLoopGroup final
            {
            public:
                EventLoopGroup(Allocator* allocator = DefaultAllocator()) noexcept;
                EventLoopGroup(uint16_t threadCount, Allocator* allocator = DefaultAllocator()) noexcept;
                ~EventLoopGroup();
                EventLoopGroup(const EventLoopGroup&) = delete;
                EventLoopGroup(EventLoopGroup&&) noexcept;
                EventLoopGroup& operator =(const EventLoopGroup&) = delete;
                EventLoopGroup& operator =(EventLoopGroup&&) noexcept;

                operator bool() const;
                int LastError() const;

                const aws_event_loop_group* GetUnderlyingHandle() const;

            private:
                aws_event_loop_group m_eventLoopGroup;
                int m_lastError;
            };
        }

    }
}




