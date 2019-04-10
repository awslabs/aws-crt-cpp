/*
 * Copyright 2010-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/io/HostResolver.h>
#include <aws/testing/aws_test_harness.h>

#include <condition_variable>
#include <mutex>

static int s_TestDefaultResolution(struct aws_allocator *allocator, void *)
{
    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
    ASSERT_TRUE(eventLoopGroup);
    ASSERT_NOT_NULL(eventLoopGroup.GetUnderlyingHandle());

    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 5, allocator);
    ASSERT_TRUE(defaultHostResolver);
    ASSERT_NOT_NULL(defaultHostResolver.GetUnderlyingHandle());

    std::condition_variable semaphore;
    std::mutex semaphoreLock;
    size_t addressCount = 0;
    int error = 0;

    auto onHostResolved =
        [&](Aws::Crt::Io::HostResolver &resolver, const Aws::Crt::Vector<aws_host_address> &addresses, int errorCode) {
            {
                std::lock_guard<std::mutex> lock(semaphoreLock);
                addressCount = addresses.size();
                error = errorCode;
            }
            semaphore.notify_one();
        };

    ASSERT_TRUE(defaultHostResolver.ResolveHost("localhost", onHostResolved));

    {
        std::unique_lock<std::mutex> lock(semaphoreLock);
        semaphore.wait(lock, [&]() { return addressCount || error; });
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(DefaultResolution, s_TestDefaultResolution)