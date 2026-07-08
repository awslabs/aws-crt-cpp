/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

/**
 * Unified endpoint resolution tests.
 *
 * s_cases and s_simple_cases are the single sources of truth.
 * Adding a case requires only adding to the relevant array.
 *
 * Each engine runs two tests:
 *   <Engine>_SimpleTest    - loops s_simple_cases (simple example-service ruleset)
 *   <Engine>_StandardTest  - loops s_cases (S3 ruleset)
 *
 * Resources under tests/resources/endpoint_engine/:
 *   s3_legacy_ruleset.c         - S3 ruleset char array compiled from aws-c-s3 (for RuleEngine, no heap alloc)
 *   bdd_ruleset.json            - S3 ruleset in BDD trait format (source of bdd_ruleset.bin)
 *   bdd_ruleset.bin             - compiled BDD bytecode (for BddEngine)
 *   simple_legacy_ruleset.json  - simple example service ruleset (for RuleEngine)
 *   partitions.json             - shared partitions config
 */

#include <aws/common/byte_buf.h>
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/endpoints/BddEngine.h>
#include <aws/crt/endpoints/RuleEngine.h>
#include <aws/testing/aws_test_harness.h>

#include <functional>

/* S3 legacy ruleset and partitions — compiled from aws-c-s3 source, no heap allocation */
extern const struct aws_byte_cursor s_s3_legacy_ruleset;
extern const struct aws_byte_cursor s_s3_legacy_partitions;

using namespace Aws::Crt;
using namespace Aws::Crt::Endpoints;

/* ------------------------------------------------------------------ */
/* Test case table                                                      */
/* ------------------------------------------------------------------ */

struct EndpointTestCase
{
    std::function<void(RequestContext &)> buildContext;
    std::function<int(const ResolutionOutcome &)> assertOutcome;
};

/* Simple example-service ruleset cases */
static const EndpointTestCase s_simple_cases[] = {
    /* SimpleRegional */
    {
        [](RequestContext &ctx) { ctx.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2")); },
        [](const ResolutionOutcome &outcome) -> int
        {
            ASSERT_TRUE(outcome.IsEndpoint());
            ASSERT_TRUE(outcome.GetUrl().has_value());
            ASSERT_TRUE(outcome.GetUrl()->compare("https://example.us-west-2.amazonaws.com") == 0);
            ASSERT_TRUE(outcome.GetHeaders().has_value());
            ASSERT_TRUE(outcome.GetHeaders()->at("x-amz-region")[0].compare("us-west-2") == 0);
            return AWS_OP_SUCCESS;
        },
    },
    /* SimpleGlobal */
    {
        [](RequestContext &ctx) { (void)ctx; },
        [](const ResolutionOutcome &outcome) -> int
        {
            ASSERT_TRUE(outcome.IsEndpoint());
            ASSERT_TRUE(outcome.GetUrl().has_value());
            ASSERT_TRUE(outcome.GetUrl()->compare("https://example.amazonaws.com") == 0);
            return AWS_OP_SUCCESS;
        },
    },
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

    /* Construct from a file path — loads ruleset from disk */
    EngineFixture(Allocator *alloc, const char *ruleset_path)
        : engine(s_loadFile(alloc, ruleset_path, ruleset, partitions), ByteCursorFromByteBuf(partitions), alloc)
    {
    }

    ~EngineFixture()
    {
        ByteBufDelete(ruleset);
        ByteBufDelete(partitions);
    }

  private:
    static ByteCursor s_loadFile(Allocator *alloc, const char *path, ByteBuf &out_ruleset, ByteBuf &out_partitions)
    {
        ByteBufInitFromFile(out_ruleset, alloc, path);
        ByteBufInitFromFile(out_partitions, alloc, "endpoint_engine/partitions.json");
        return ByteCursorFromByteBuf(out_ruleset);
    }
};

template <> struct EngineFixture<BddEngine>
{
    ByteBuf bytecode;
    ByteBuf partitions;
    BddEngine engine;

    EngineFixture(Allocator *alloc, const char *bytecode_path)
        : engine(s_load(alloc, bytecode_path, bytecode, partitions), ByteCursorFromByteBuf(partitions), alloc)
    {
    }

    ~EngineFixture()
    {
        ByteBufDelete(bytecode);
        ByteBufDelete(partitions);
    }

  private:
    static ByteCursor s_load(Allocator *alloc, const char *path, ByteBuf &out_bytecode, ByteBuf &out_partitions)
    {
        ByteBufInitFromFile(out_bytecode, alloc, path);
        ByteBufInitFromFile(out_partitions, alloc, "endpoint_engine/partitions.json");
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
/* RuleEngine tests — simple ruleset                                   */
/* ------------------------------------------------------------------ */

static int s_TestRuleEngine_SimpleRegional(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    EngineFixture<RuleEngine> engineFixture(allocator, "endpoint_engine/simple_legacy_ruleset.json");
    ASSERT_TRUE(engineFixture.engine);
    return s_RunCase(allocator, s_simple_cases[0], engineFixture.engine);
}
AWS_TEST_CASE(RuleEngine_SimpleRegional, s_TestRuleEngine_SimpleRegional)

static int s_TestRuleEngine_SimpleGlobal(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);
    EngineFixture<RuleEngine> engineFixture(allocator, "endpoint_engine/simple_legacy_ruleset.json");
    ASSERT_TRUE(engineFixture.engine);
    return s_RunCase(allocator, s_simple_cases[1], engineFixture.engine);
}
AWS_TEST_CASE(RuleEngine_SimpleGlobal, s_TestRuleEngine_SimpleGlobal)

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
