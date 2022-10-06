/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/endpoints/RuleEngine.h>
#include <aws/testing/aws_test_harness.h>

static int s_TestRuleEngine(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(RuleEngine, s_TestRuleEngine)
