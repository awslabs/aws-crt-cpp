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
#include <aws/crt/Api.h>
#include <aws/testing/aws_test_harness.h>

#include <utility>

static int s_TestClientBootstrapResourceSafety(struct aws_allocator *allocator, void *)
{
    Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
    ASSERT_TRUE(eventLoopGroup);
    ASSERT_NOT_NULL(eventLoopGroup.GetUnderlyingHandle());

    Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, allocator);
    ASSERT_TRUE(clientBootstrap);
    ASSERT_NOT_NULL(clientBootstrap.GetUnderlyingHandle());

    Aws::Crt::Io::ClientBootstrap clientBootstrapMoved = std::move(clientBootstrap);
    ASSERT_TRUE(clientBootstrapMoved);
    ASSERT_NOT_NULL(clientBootstrapMoved.GetUnderlyingHandle());

    // NOLINTNEXTLINE
    ASSERT_FALSE(clientBootstrap);

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(ClientBootstrapResourceSafety, s_TestClientBootstrapResourceSafety)
