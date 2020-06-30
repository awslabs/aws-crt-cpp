/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
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
