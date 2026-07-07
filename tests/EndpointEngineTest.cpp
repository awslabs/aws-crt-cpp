/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

/**
 * Unified endpoint resolution tests.
 *
 * Each case runs through both RuleEngine (legacy JSON ruleset) and BddEngine
 * (compiled bytecode). The s_cases table is the single source of truth —
 * adding a new case automatically covers both engines.
 *
 * Resources under tests/resources/endpoint_engine/:
 *   legacy_ruleset.json  - S3 ruleset in legacy tree format (for RuleEngine)
 *   bdd_ruleset.json     - S3 ruleset in BDD trait format (source of bdd_ruleset.bin)
 *   bdd_ruleset.bin      - compiled BDD bytecode (for BddEngine)
 *   partitions.json      - shared partitions config
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
    const char *name;
    std::function<void(RequestContext &)> buildContext;
    std::function<int(const ResolutionOutcome &)> assertOutcome;
};

static const EndpointTestCase s_cases[] = {
    {
        "virtual_hosted",
        [](RequestContext &ctx)
        {
            ctx.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2"));
            ctx.AddString(ByteCursorFromCString("Bucket"), ByteCursorFromCString("bucket-name"));
        },
        [](const ResolutionOutcome &outcome) -> int
        {
            ASSERT_TRUE(outcome.IsEndpoint());
            ASSERT_TRUE(outcome.GetUrl().has_value());
            ASSERT_TRUE(outcome.GetUrl()->compare("https://bucket-name.s3.us-west-2.amazonaws.com") == 0);
            return AWS_OP_SUCCESS;
        },
    },
    {
        "path_style",
        [](RequestContext &ctx)
        {
            ctx.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2"));
            ctx.AddBoolean(ByteCursorFromCString("ForcePathStyle"), true);
            ctx.AddString(ByteCursorFromCString("Bucket"), ByteCursorFromCString("bucket-name"));
        },
        [](const ResolutionOutcome &outcome) -> int
        {
            ASSERT_TRUE(outcome.IsEndpoint());
            ASSERT_TRUE(outcome.GetUrl().has_value());
            ASSERT_TRUE(outcome.GetUrl()->compare("https://s3.us-west-2.amazonaws.com/bucket-name") == 0);
            return AWS_OP_SUCCESS;
        },
    },
    {
        "dataplane_zone",
        [](RequestContext &ctx)
        {
            ctx.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-east-1"));
            ctx.AddString(ByteCursorFromCString("Bucket"), ByteCursorFromCString("mybucket--abcd-ab1--x-s3"));
        },
        [](const ResolutionOutcome &outcome) -> int
        {
            ASSERT_TRUE(outcome.IsEndpoint());
            ASSERT_TRUE(outcome.GetUrl().has_value());
            ASSERT_TRUE(
                outcome.GetUrl()->compare(
                    "https://mybucket--abcd-ab1--x-s3.s3express-abcd-ab1.us-east-1.amazonaws.com") == 0);
            return AWS_OP_SUCCESS;
        },
    },
    {
        "access_point",
        [](RequestContext &ctx)
        {
            ctx.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2"));
            ctx.AddString(
                ByteCursorFromCString("Bucket"),
                ByteCursorFromCString("arn:aws:s3:us-west-2:123456789012:accesspoint:myendpoint"));
        },
        [](const ResolutionOutcome &outcome) -> int
        {
            ASSERT_TRUE(outcome.IsEndpoint());
            ASSERT_TRUE(outcome.GetUrl().has_value());
            ASSERT_TRUE(
                outcome.GetUrl()->compare("https://myendpoint-123456789012.s3-accesspoint.us-west-2.amazonaws.com") ==
                0);
            return AWS_OP_SUCCESS;
        },
    },
    {
        "outpost",
        [](RequestContext &ctx)
        {
            ctx.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2"));
            ctx.AddString(
                ByteCursorFromCString("Bucket"),
                ByteCursorFromCString(
                    "arn:aws:s3-outposts:us-west-2:123456789012:outpost/op-01234567890123456/accesspoint/reports"));
        },
        [](const ResolutionOutcome &outcome) -> int
        {
            ASSERT_TRUE(outcome.IsEndpoint());
            ASSERT_TRUE(outcome.GetUrl().has_value());
            ASSERT_TRUE(
                outcome.GetUrl()->compare(
                    "https://reports-123456789012.op-01234567890123456.s3-outposts.us-west-2.amazonaws.com") == 0);
            return AWS_OP_SUCCESS;
        },
    },
};

/* ------------------------------------------------------------------ */
/* Shared runner                                                        */
/* ------------------------------------------------------------------ */

template <typename Engine> static int s_RunCase(Allocator *allocator, const EndpointTestCase &tc, const Engine &engine)
{
    RequestContext ctx(allocator);
    tc.buildContext(ctx);
    auto result = engine.Resolve(ctx);
    ASSERT_TRUE(result.has_value());
    return tc.assertOutcome(*result);
}

/* ------------------------------------------------------------------ */
/* RuleEngine tests                                                     */
/* ------------------------------------------------------------------ */

static int s_TestRuleEngine_VirtualHosted(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    ByteBuf ruleset, partitions;
    ASSERT_TRUE(ByteBufInitFromFile(ruleset, allocator, "endpoint_engine/legacy_ruleset.json"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));
    RuleEngine engine(ByteCursorFromByteBuf(ruleset), ByteCursorFromByteBuf(partitions), allocator);
    ASSERT_TRUE(engine);
    int r = s_RunCase(allocator, s_cases[0], engine);
    ByteBufDelete(ruleset);
    ByteBufDelete(partitions);
    return r;
}
AWS_TEST_CASE(RuleEngine_VirtualHosted, s_TestRuleEngine_VirtualHosted)

static int s_TestRuleEngine_PathStyle(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    ByteBuf ruleset, partitions;
    ASSERT_TRUE(ByteBufInitFromFile(ruleset, allocator, "endpoint_engine/legacy_ruleset.json"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));
    RuleEngine engine(ByteCursorFromByteBuf(ruleset), ByteCursorFromByteBuf(partitions), allocator);
    ASSERT_TRUE(engine);
    int r = s_RunCase(allocator, s_cases[1], engine);
    ByteBufDelete(ruleset);
    ByteBufDelete(partitions);
    return r;
}
AWS_TEST_CASE(RuleEngine_PathStyle, s_TestRuleEngine_PathStyle)

static int s_TestRuleEngine_DataplaneZone(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    ByteBuf ruleset, partitions;
    ASSERT_TRUE(ByteBufInitFromFile(ruleset, allocator, "endpoint_engine/legacy_ruleset.json"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));
    RuleEngine engine(ByteCursorFromByteBuf(ruleset), ByteCursorFromByteBuf(partitions), allocator);
    ASSERT_TRUE(engine);
    int r = s_RunCase(allocator, s_cases[2], engine);
    ByteBufDelete(ruleset);
    ByteBufDelete(partitions);
    return r;
}
AWS_TEST_CASE(RuleEngine_DataplaneZone, s_TestRuleEngine_DataplaneZone)

static int s_TestRuleEngine_AccessPoint(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    ByteBuf ruleset, partitions;
    ASSERT_TRUE(ByteBufInitFromFile(ruleset, allocator, "endpoint_engine/legacy_ruleset.json"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));
    RuleEngine engine(ByteCursorFromByteBuf(ruleset), ByteCursorFromByteBuf(partitions), allocator);
    ASSERT_TRUE(engine);
    int r = s_RunCase(allocator, s_cases[3], engine);
    ByteBufDelete(ruleset);
    ByteBufDelete(partitions);
    return r;
}
AWS_TEST_CASE(RuleEngine_AccessPoint, s_TestRuleEngine_AccessPoint)

static int s_TestRuleEngine_Outpost(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    ByteBuf ruleset, partitions;
    ASSERT_TRUE(ByteBufInitFromFile(ruleset, allocator, "endpoint_engine/legacy_ruleset.json"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));
    RuleEngine engine(ByteCursorFromByteBuf(ruleset), ByteCursorFromByteBuf(partitions), allocator);
    ASSERT_TRUE(engine);
    int r = s_RunCase(allocator, s_cases[4], engine);
    ByteBufDelete(ruleset);
    ByteBufDelete(partitions);
    return r;
}
AWS_TEST_CASE(RuleEngine_Outpost, s_TestRuleEngine_Outpost)

/* ------------------------------------------------------------------ */
/* BddEngine tests                                                      */
/* ------------------------------------------------------------------ */

static int s_TestBddEngine_VirtualHosted(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    ByteBuf bytecode, partitions;
    ASSERT_TRUE(ByteBufInitFromFile(bytecode, allocator, "endpoint_engine/bdd_ruleset.bin"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));
    BddEngine engine(ByteCursorFromByteBuf(bytecode), ByteCursorFromByteBuf(partitions), allocator);
    ASSERT_TRUE(engine);
    int r = s_RunCase(allocator, s_cases[0], engine);
    ByteBufDelete(bytecode);
    ByteBufDelete(partitions);
    return r;
}
AWS_TEST_CASE(BddEngine_VirtualHosted, s_TestBddEngine_VirtualHosted)

static int s_TestBddEngine_PathStyle(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    ByteBuf bytecode, partitions;
    ASSERT_TRUE(ByteBufInitFromFile(bytecode, allocator, "endpoint_engine/bdd_ruleset.bin"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));
    BddEngine engine(ByteCursorFromByteBuf(bytecode), ByteCursorFromByteBuf(partitions), allocator);
    ASSERT_TRUE(engine);
    int r = s_RunCase(allocator, s_cases[1], engine);
    ByteBufDelete(bytecode);
    ByteBufDelete(partitions);
    return r;
}
AWS_TEST_CASE(BddEngine_PathStyle, s_TestBddEngine_PathStyle)

static int s_TestBddEngine_DataplaneZone(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    ByteBuf bytecode, partitions;
    ASSERT_TRUE(ByteBufInitFromFile(bytecode, allocator, "endpoint_engine/bdd_ruleset.bin"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));
    BddEngine engine(ByteCursorFromByteBuf(bytecode), ByteCursorFromByteBuf(partitions), allocator);
    ASSERT_TRUE(engine);
    int r = s_RunCase(allocator, s_cases[2], engine);
    ByteBufDelete(bytecode);
    ByteBufDelete(partitions);
    return r;
}
AWS_TEST_CASE(BddEngine_DataplaneZone, s_TestBddEngine_DataplaneZone)

static int s_TestBddEngine_AccessPoint(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    ByteBuf bytecode, partitions;
    ASSERT_TRUE(ByteBufInitFromFile(bytecode, allocator, "endpoint_engine/bdd_ruleset.bin"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));
    BddEngine engine(ByteCursorFromByteBuf(bytecode), ByteCursorFromByteBuf(partitions), allocator);
    ASSERT_TRUE(engine);
    int r = s_RunCase(allocator, s_cases[3], engine);
    ByteBufDelete(bytecode);
    ByteBufDelete(partitions);
    return r;
}
AWS_TEST_CASE(BddEngine_AccessPoint, s_TestBddEngine_AccessPoint)

static int s_TestBddEngine_Outpost(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    ByteBuf bytecode, partitions;
    ASSERT_TRUE(ByteBufInitFromFile(bytecode, allocator, "endpoint_engine/bdd_ruleset.bin"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));
    BddEngine engine(ByteCursorFromByteBuf(bytecode), ByteCursorFromByteBuf(partitions), allocator);
    ASSERT_TRUE(engine);
    int r = s_RunCase(allocator, s_cases[4], engine);
    ByteBufDelete(bytecode);
    ByteBufDelete(partitions);
    return r;
}
AWS_TEST_CASE(BddEngine_Outpost, s_TestBddEngine_Outpost)

/* ------------------------------------------------------------------ */
/* Simple ruleset cases (example service, Region only)                 */
/* ------------------------------------------------------------------ */

static const EndpointTestCase s_simple_cases[] = {
    {
        "simple_regional",
        [](RequestContext &ctx) {
            ctx.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2"));
        },
        [](const ResolutionOutcome &outcome) -> int {
            ASSERT_TRUE(outcome.IsEndpoint());
            ASSERT_TRUE(outcome.GetUrl().has_value());
            ASSERT_TRUE(outcome.GetUrl()->compare("https://example.us-west-2.amazonaws.com") == 0);
            ASSERT_TRUE(outcome.GetHeaders().has_value());
            ASSERT_TRUE(outcome.GetHeaders()->at("x-amz-region")[0].compare("us-west-2") == 0);
            return AWS_OP_SUCCESS;
        },
    },
    {
        "simple_global",
        [](RequestContext &ctx) { (void)ctx; },
        [](const ResolutionOutcome &outcome) -> int {
            ASSERT_TRUE(outcome.IsEndpoint());
            ASSERT_TRUE(outcome.GetUrl().has_value());
            ASSERT_TRUE(outcome.GetUrl()->compare("https://example.amazonaws.com") == 0);
            return AWS_OP_SUCCESS;
        },
    },
};

static int s_MakeSimpleRuleEngine(Allocator *allocator, ByteBuf &ruleset, ByteBuf &partitions, RuleEngine &out)
{
    ASSERT_TRUE(ByteBufInitFromFile(ruleset, allocator, "endpoint_engine/simple_legacy_ruleset.json"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));
    out = RuleEngine(ByteCursorFromByteBuf(ruleset), ByteCursorFromByteBuf(partitions), allocator);
    ASSERT_TRUE(out);
    return AWS_OP_SUCCESS;
}

static int s_MakeSimpleBddEngine(Allocator *allocator, ByteBuf &bytecode, ByteBuf &partitions, BddEngine &out)
{
    ASSERT_TRUE(ByteBufInitFromFile(bytecode, allocator, "endpoint_engine/simple_bdd_ruleset.bin"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));
    out = BddEngine(ByteCursorFromByteBuf(bytecode), ByteCursorFromByteBuf(partitions), allocator);
    ASSERT_TRUE(out);
    return AWS_OP_SUCCESS;
}

static int s_TestRuleEngine_SimpleRegional(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    ByteBuf ruleset, partitions;
    RuleEngine engine;
    ASSERT_SUCCESS(s_MakeSimpleRuleEngine(allocator, ruleset, partitions, engine));
    int r = s_RunCase(allocator, s_simple_cases[0], engine);
    ByteBufDelete(ruleset);
    ByteBufDelete(partitions);
    return r;
}
AWS_TEST_CASE(RuleEngine_SimpleRegional, s_TestRuleEngine_SimpleRegional)

static int s_TestRuleEngine_SimpleGlobal(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    ByteBuf ruleset, partitions;
    RuleEngine engine;
    ASSERT_SUCCESS(s_MakeSimpleRuleEngine(allocator, ruleset, partitions, engine));
    int r = s_RunCase(allocator, s_simple_cases[1], engine);
    ByteBufDelete(ruleset);
    ByteBufDelete(partitions);
    return r;
}
AWS_TEST_CASE(RuleEngine_SimpleGlobal, s_TestRuleEngine_SimpleGlobal)

static int s_TestBddEngine_SimpleRegional(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    ByteBuf bytecode, partitions;
    BddEngine engine;
    ASSERT_SUCCESS(s_MakeSimpleBddEngine(allocator, bytecode, partitions, engine));
    int r = s_RunCase(allocator, s_simple_cases[0], engine);
    ByteBufDelete(bytecode);
    ByteBufDelete(partitions);
    return r;
}
AWS_TEST_CASE(BddEngine_SimpleRegional, s_TestBddEngine_SimpleRegional)

static int s_TestBddEngine_SimpleGlobal(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    ByteBuf bytecode, partitions;
    BddEngine engine;
    ASSERT_SUCCESS(s_MakeSimpleBddEngine(allocator, bytecode, partitions, engine));
    int r = s_RunCase(allocator, s_simple_cases[1], engine);
    ByteBufDelete(bytecode);
    ByteBufDelete(partitions);
    return r;
}
AWS_TEST_CASE(BddEngine_SimpleGlobal, s_TestBddEngine_SimpleGlobal)
