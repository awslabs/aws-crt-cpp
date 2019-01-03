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

static int s_TestTLSContextResourceSafety(Aws::Crt::Allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);
    Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();

    Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    ASSERT_TRUE(tlsContext);

    auto tlsContextPostMove = std::move(tlsContext);
    ASSERT_TRUE(tlsContextPostMove);

    // NOLINTNEXTLINE
    ASSERT_FALSE(tlsContext);

    auto tlsConnectionOptions = tlsContextPostMove.NewConnectionOptions();

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(TLSContextResourceSafety, s_TestTLSContextResourceSafety)
