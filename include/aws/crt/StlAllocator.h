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

#include <memory>

namespace Aws
{
    namespace Crt
    {
        extern Allocator* g_allocator;

        template<typename T>
        class StlAllocator final : public std::allocator<T>
        {
        public:
            using Base = std::allocator<T>;

            StlAllocator() noexcept : Base() {}
            StlAllocator(const StlAllocator<T>& a) noexcept : Base(a) {}
            
            template<class U>
            StlAllocator(const StlAllocator<U>& a) noexcept : Base(a) {}

            ~StlAllocator() {}

            using sizeType = std::size_t;

            template<typename U>
            struct rebind
            {
                typedef StlAllocator<U> other;
            };

            typename Base::pointer allocate(size_type n, const void* hint = nullptr)
            {
                (void)hint;
                assert(g_allocator);
                return reinterpret_cast<typename Base::pointer>(aws_mem_acquire(g_allocator, n * sizeof(T)));
            }

            void deallocate(typename Base::pointer p, size_type)
            {
                assert(g_allocator);
                aws_mem_release(g_allocator, p);
            }
        };
    }
}
