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

#include <aws/testing/aws_test_harness.h>

#include <utility>

static int s_TestEventLoopResourceSafety(struct aws_allocator *allocator, void *)
{
    Aws::Crt::Io::EventLoopGroup eventLoopGroup(allocator, 0);
    ASSERT_TRUE(eventLoopGroup);
    ASSERT_NOT_NULL(eventLoopGroup.GetUnderlyingHandle());

    Aws::Crt::Io::EventLoopGroup eventLoopGroupPostMove(std::move(eventLoopGroup));
    ASSERT_TRUE(eventLoopGroupPostMove);
    ASSERT_NOT_NULL(eventLoopGroupPostMove.GetUnderlyingHandle());

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(EventLoopResourceSafety, s_TestEventLoopResourceSafety)
