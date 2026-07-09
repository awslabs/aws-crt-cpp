/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

/**
 * Unified endpoint resolution tests.
 *
 * Both RuleEngine and BddEngine are tested against an equivalent simple ruleset:
 *   Region set   -> https://example.{Region}.amazonaws.com
 *   Region unset -> https://example.amazonaws.com
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

using namespace Aws::Crt;
using namespace Aws::Crt::Endpoints;

static int s_TestRuleEngine_RegionalEndpoint(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);

    ByteBuf ruleset, partitions;
    ASSERT_TRUE(ByteBufInitFromFile(ruleset, allocator, "endpoint_engine/model.json"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));

    RuleEngine engine(ByteCursorFromByteBuf(ruleset), ByteCursorFromByteBuf(partitions), allocator);
    ByteBufDelete(ruleset);
    ByteBufDelete(partitions);
    ASSERT_TRUE(engine);

    RequestContext context(allocator);
    context.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2"));

    auto resolved = engine.Resolve(context);
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(resolved->IsEndpoint());
    ASSERT_TRUE(resolved->GetUrl()->compare("https://example.us-west-2.amazonaws.com") == 0);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(RuleEngine_RegionalEndpoint, s_TestRuleEngine_RegionalEndpoint)

static int s_TestRuleEngine_GlobalEndpoint(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);

    ByteBuf ruleset, partitions;
    ASSERT_TRUE(ByteBufInitFromFile(ruleset, allocator, "endpoint_engine/model.json"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));

    RuleEngine engine(ByteCursorFromByteBuf(ruleset), ByteCursorFromByteBuf(partitions), allocator);
    ByteBufDelete(ruleset);
    ByteBufDelete(partitions);
    ASSERT_TRUE(engine);

    RequestContext context(allocator); /* no Region */

    auto resolved = engine.Resolve(context);
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(resolved->IsEndpoint());
    ASSERT_TRUE(resolved->GetUrl()->compare("https://example.amazonaws.com") == 0);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(RuleEngine_GlobalEndpoint, s_TestRuleEngine_GlobalEndpoint)

static int s_TestBddEngine_RegionalEndpoint(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);

    ByteBuf bytecode, partitions;
    ASSERT_TRUE(ByteBufInitFromFile(bytecode, allocator, "endpoint_engine/model.bin"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));

    BddEngine engine(ByteCursorFromByteBuf(bytecode), ByteCursorFromByteBuf(partitions), allocator);
    ASSERT_TRUE(engine);

    RequestContext context(allocator);
    context.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2"));

    auto resolved = engine.Resolve(context);
    ByteBufDelete(bytecode);
    ByteBufDelete(partitions);
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(resolved->IsEndpoint());
    ASSERT_TRUE(resolved->GetUrl()->compare("https://example.us-west-2.amazonaws.com") == 0);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(BddEngine_RegionalEndpoint, s_TestBddEngine_RegionalEndpoint)

static int s_TestBddEngine_GlobalEndpoint(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);

    ByteBuf bytecode, partitions;
    ASSERT_TRUE(ByteBufInitFromFile(bytecode, allocator, "endpoint_engine/model.bin"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));

    BddEngine engine(ByteCursorFromByteBuf(bytecode), ByteCursorFromByteBuf(partitions), allocator);
    ASSERT_TRUE(engine);

    RequestContext context(allocator); /* no Region */

    auto resolved = engine.Resolve(context);
    ByteBufDelete(bytecode);
    ByteBufDelete(partitions);
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(resolved->IsEndpoint());
    ASSERT_TRUE(resolved->GetUrl()->compare("https://example.amazonaws.com") == 0);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(BddEngine_GlobalEndpoint, s_TestBddEngine_GlobalEndpoint)
