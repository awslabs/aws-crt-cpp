/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

/**
 * RuleEngine-specific tests.
 * Endpoint resolution cases live in EndpointEngineTest.cpp.
 */

#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/endpoints/RuleEngine.h>
#include <aws/testing/aws_test_harness.h>

using namespace Aws::Crt;

static int s_TestRuleEngineContextParams(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);

    Endpoints::RequestContext context(allocator);
    context.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2"));
    context.AddBoolean(ByteCursorFromCString("AValidBoolParam"), false);
    context.AddStringArray(ByteCursorFromCString("StringArray1"), {});
    context.AddStringArray(
        ByteCursorFromCString("StringArray2"), {ByteCursorFromCString("a"), ByteCursorFromCString("b")});

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(RuleEngineContextParams, s_TestRuleEngineContextParams)
