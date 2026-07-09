/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

/**
 * Unified endpoint resolution tests.
 *
 * s_cases is the single source of truth — adding a case automatically covers both engines.
 *
 * Resources under tests/resources/endpoint_engine/:
 *   s3_legacy_ruleset.cpp    - S3 ruleset char array compiled from aws-c-s3 (for RuleEngine, no heap alloc)
 *   s3_legacy_partitions.cpp - S3 partitions char array compiled from aws-c-s3 (shared, no heap alloc)
 *   bdd_ruleset.json         - S3 ruleset in BDD trait format (source of bdd_ruleset.bin)
 *   bdd_ruleset.bin          - compiled BDD bytecode (for BddEngine)
 */

#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/endpoints/BddEngine.h>
#include <aws/crt/endpoints/RuleEngine.h>
#include <aws/testing/aws_test_harness.h>

#include <functional>

using namespace Aws::Crt;
using namespace Aws::Crt::Endpoints;

/* S3 legacy ruleset and partitions — compiled from aws-c-s3 source, no heap allocation */
extern const ByteCursor s_s3_legacy_ruleset;
extern const ByteCursor s_s3_legacy_partitions;

/* ------------------------------------------------------------------ */
/* Test case table                                                      */
/* ------------------------------------------------------------------ */

struct EndpointTestCase
{
    std::function<void(RequestContext &)> buildContext;
    std::function<int(const ResolutionOutcome &)> assertOutcome;
};

/* S3 standard ruleset cases */
static const EndpointTestCase s_cases[] = {
    /* VirtualHosted */
    {
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
    /* PathStyle */
    {
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
    /* DataplaneZone */
    {
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
    /* AccessPoint */
    {
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
    /* Outpost */
    {
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
/* Engine fixtures — own bufs, construct engine, clean up on destruct  */
/* ------------------------------------------------------------------ */

template <typename Engine> struct EngineFixture;

template <> struct EngineFixture<RuleEngine>
{
    ByteBuf ruleset;
    ByteBuf partitions;
    RuleEngine engine;

    /* Construct from a cursor — no heap allocation for ruleset or partitions */
    EngineFixture(Allocator *alloc, ByteCursor rulesetCursor) : engine(rulesetCursor, s_s3_legacy_partitions, alloc)
    {
        AWS_ZERO_STRUCT(ruleset);
        AWS_ZERO_STRUCT(partitions);
    }

    ~EngineFixture()
    {
        ByteBufDelete(ruleset);
        ByteBufDelete(partitions);
    }
};

template <> struct EngineFixture<BddEngine>
{
    ByteBuf bytecode;
    BddEngine engine;

    EngineFixture(Allocator *alloc, const char *bytecode_path)
        : engine(s_load(alloc, bytecode_path, bytecode), s_s3_legacy_partitions, alloc)
    {
    }

    ~EngineFixture() { ByteBufDelete(bytecode); }

  private:
    static ByteCursor s_load(Allocator *alloc, const char *path, ByteBuf &out_bytecode)
    {
        ByteBufInitFromFile(out_bytecode, alloc, path);
        return ByteCursorFromByteBuf(out_bytecode);
    }
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
/* RuleEngine tests — S3 ruleset                                       */
/* ------------------------------------------------------------------ */

static int s_TestRuleEngine_VirtualHosted(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    EngineFixture<RuleEngine> engineFixture(allocator, s_s3_legacy_ruleset);
    ASSERT_TRUE(engineFixture.engine);
    return s_RunCase(allocator, s_cases[0], engineFixture.engine);
}
AWS_TEST_CASE(RuleEngine_VirtualHosted, s_TestRuleEngine_VirtualHosted)

static int s_TestRuleEngine_PathStyle(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    EngineFixture<RuleEngine> engineFixture(allocator, s_s3_legacy_ruleset);
    ASSERT_TRUE(engineFixture.engine);
    return s_RunCase(allocator, s_cases[1], engineFixture.engine);
}
AWS_TEST_CASE(RuleEngine_PathStyle, s_TestRuleEngine_PathStyle)

static int s_TestRuleEngine_DataplaneZone(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    EngineFixture<RuleEngine> engineFixture(allocator, s_s3_legacy_ruleset);
    ASSERT_TRUE(engineFixture.engine);
    return s_RunCase(allocator, s_cases[2], engineFixture.engine);
}
AWS_TEST_CASE(RuleEngine_DataplaneZone, s_TestRuleEngine_DataplaneZone)

static int s_TestRuleEngine_AccessPoint(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    EngineFixture<RuleEngine> engineFixture(allocator, s_s3_legacy_ruleset);
    ASSERT_TRUE(engineFixture.engine);
    return s_RunCase(allocator, s_cases[3], engineFixture.engine);
}
AWS_TEST_CASE(RuleEngine_AccessPoint, s_TestRuleEngine_AccessPoint)

static int s_TestRuleEngine_Outpost(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    EngineFixture<RuleEngine> engineFixture(allocator, s_s3_legacy_ruleset);
    ASSERT_TRUE(engineFixture.engine);
    return s_RunCase(allocator, s_cases[4], engineFixture.engine);
}
AWS_TEST_CASE(RuleEngine_Outpost, s_TestRuleEngine_Outpost)

/* ------------------------------------------------------------------ */
/* BddEngine tests — S3 ruleset                                        */
/* ------------------------------------------------------------------ */

static int s_TestBddEngine_VirtualHosted(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    EngineFixture<BddEngine> engineFixture(allocator, "endpoint_engine/bdd_ruleset.bin");
    ASSERT_TRUE(engineFixture.engine);
    return s_RunCase(allocator, s_cases[0], engineFixture.engine);
}
AWS_TEST_CASE(BddEngine_VirtualHosted, s_TestBddEngine_VirtualHosted)

static int s_TestBddEngine_PathStyle(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    EngineFixture<BddEngine> engineFixture(allocator, "endpoint_engine/bdd_ruleset.bin");
    ASSERT_TRUE(engineFixture.engine);
    return s_RunCase(allocator, s_cases[1], engineFixture.engine);
}
AWS_TEST_CASE(BddEngine_PathStyle, s_TestBddEngine_PathStyle)

static int s_TestBddEngine_DataplaneZone(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    EngineFixture<BddEngine> engineFixture(allocator, "endpoint_engine/bdd_ruleset.bin");
    ASSERT_TRUE(engineFixture.engine);
    return s_RunCase(allocator, s_cases[2], engineFixture.engine);
}
AWS_TEST_CASE(BddEngine_DataplaneZone, s_TestBddEngine_DataplaneZone)

static int s_TestBddEngine_AccessPoint(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    EngineFixture<BddEngine> engineFixture(allocator, "endpoint_engine/bdd_ruleset.bin");
    ASSERT_TRUE(engineFixture.engine);
    return s_RunCase(allocator, s_cases[3], engineFixture.engine);
}
AWS_TEST_CASE(BddEngine_AccessPoint, s_TestBddEngine_AccessPoint)

static int s_TestBddEngine_Outpost(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    EngineFixture<BddEngine> engineFixture(allocator, "endpoint_engine/bdd_ruleset.bin");
    ASSERT_TRUE(engineFixture.engine);
    return s_RunCase(allocator, s_cases[4], engineFixture.engine);
}
AWS_TEST_CASE(BddEngine_Outpost, s_TestBddEngine_Outpost)
