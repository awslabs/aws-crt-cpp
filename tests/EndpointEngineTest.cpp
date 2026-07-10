/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

/**
 * Unified endpoint resolution tests.
 *
 * Each test exercises both RuleEngine and BddEngine against the same case.
 * A test fails if either engine produces the wrong result.
 *
 * Resources under tests/resources/endpoint_engine/:
 *   model.json      - simple ruleset in legacy tree format (for RuleEngine)
 *   model_bdd.json  - same ruleset in BDD trait format (source of model.bin)
 *   model.bin       - compiled BDD bytecode (for BddEngine)
 *   partitions.json - partitions config
 */

#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/endpoints/BddEngine.h>
#include <aws/crt/endpoints/RuleEngine.h>
#include <aws/testing/aws_test_harness.h>

#include <functional>

using namespace Aws::Crt;
using namespace Aws::Crt::Endpoints;

struct EndpointTestCase
{
    std::function<void(RequestContext &)> buildContext;
    std::function<int(const ResolutionOutcome &)> assertOutcome;
};

template <typename Engine> static int s_RunCase(Allocator *allocator, const EndpointTestCase &tc, const Engine &engine)
{
    RequestContext ctx(allocator);
    tc.buildContext(ctx);
    auto result = engine.Resolve(ctx);
    ASSERT_TRUE(result.has_value());
    return tc.assertOutcome(*result);
}

static int s_RunBothEngines(Allocator *allocator, const EndpointTestCase &tc)
{
    ByteBuf ruleset, bytecode, partitions;
    ASSERT_TRUE(ByteBufInitFromFile(ruleset, allocator, "endpoint_engine/model.json"));
    ASSERT_TRUE(ByteBufInitFromFile(bytecode, allocator, "endpoint_engine/model.bin"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));

    RuleEngine ruleEngine(ByteCursorFromByteBuf(ruleset), ByteCursorFromByteBuf(partitions), allocator);
    BddEngine bddEngine(ByteCursorFromByteBuf(bytecode), ByteCursorFromByteBuf(partitions), allocator);

    ASSERT_TRUE(ruleEngine);
    ASSERT_TRUE(bddEngine);

    ASSERT_SUCCESS(s_RunCase(allocator, tc, ruleEngine));
    ASSERT_SUCCESS(s_RunCase(allocator, tc, bddEngine));

    ByteBufDelete(ruleset);
    ByteBufDelete(bytecode);
    ByteBufDelete(partitions);

    return AWS_OP_SUCCESS;
}

/* ------------------------------------------------------------------ */
/* Test cases                                                           */
/* ------------------------------------------------------------------ */

static int s_TestEndpointResolution_RegionalEndpoint(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    return s_RunBothEngines(
        allocator,
        {
            [](RequestContext &c) { c.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2")); },
            [](const ResolutionOutcome &outcome) -> int
            {
                ASSERT_TRUE(outcome.IsEndpoint());
                ASSERT_TRUE(outcome.GetUrl().has_value());
                ASSERT_TRUE(outcome.GetUrl()->compare("https://example.us-west-2.amazonaws.com") == 0);
                return AWS_OP_SUCCESS;
            },
        });
}
AWS_TEST_CASE(EndpointResolution_RegionalEndpoint, s_TestEndpointResolution_RegionalEndpoint)

static int s_TestEndpointResolution_GlobalEndpoint(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    return s_RunBothEngines(
        allocator,
        {
            [](RequestContext &c)
            {
                (void)c; /* no Region */
            },
            [](const ResolutionOutcome &outcome) -> int
            {
                ASSERT_TRUE(outcome.IsEndpoint());
                ASSERT_TRUE(outcome.GetUrl().has_value());
                ASSERT_TRUE(outcome.GetUrl()->compare("https://example.amazonaws.com") == 0);
                return AWS_OP_SUCCESS;
            },
        });
}
AWS_TEST_CASE(EndpointResolution_GlobalEndpoint, s_TestEndpointResolution_GlobalEndpoint)
