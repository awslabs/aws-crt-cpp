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

#include <memory>

#include <aws/common/common.h>

namespace Aws
{
    namespace Crt
    {
        using Allocator = aws_allocator;
        extern Allocator *g_allocator;

        template <typename T> class StlAllocator : public std::allocator<T>
        {
          public:
            using Base = std::allocator<T>;
#ifdef NEVER
#ifndef AWS_CRT_UNIT_TESTS
            StlAllocator() noexcept :
                Base(),
                m_allocator(g_allocator)
            {}
#endif /* AWS_CRT_UNIT_TESTS */
#endif
            StlAllocator(Allocator *a) :
                Base(),
                m_allocator(a)
            {}

            StlAllocator(const StlAllocator<T> &a) noexcept :
                Base(a),
                m_allocator(a.m_allocator)
            {}

            template <typename U> friend class StlAllocator;

            template <class U> StlAllocator(const StlAllocator<U> &a) noexcept :
                Base(a),
                m_allocator(a.m_allocator)
            {}

            ~StlAllocator() {}

            using size_type = std::size_t;

            template <typename U> struct rebind
            {
                typedef StlAllocator<U> other;
            };

            typename Base::pointer allocate(size_type n, const void *hint = nullptr)
            {
                (void)hint;
                AWS_ASSERT(m_allocator);
                return reinterpret_cast<typename Base::pointer>(aws_mem_acquire(m_allocator, n * sizeof(T)));
            }

            void deallocate(typename Base::pointer p, size_type)
            {
                AWS_ASSERT(m_allocator);
                aws_mem_release(m_allocator, p);
            }

          private:

            Allocator *m_allocator;
        };
    } // namespace Crt
} // namespace Aws
