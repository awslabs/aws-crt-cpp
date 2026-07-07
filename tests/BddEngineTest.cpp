/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

/**
 * BddEngine-specific tests.
 * Endpoint resolution cases live in EndpointEngineTest.cpp.
 */

#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/endpoints/BddEngine.h>
#include <aws/testing/aws_test_harness.h>

using namespace Aws::Crt;

AWS_TEST_CASE(BddEngineInvalidBytecode, s_TestBddEngineInvalidBytecode)
static int s_TestBddEngineInvalidBytecode(Allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);

    ByteBuf partitions_buf;
    ASSERT_TRUE(ByteBufInitFromFile(partitions_buf, allocator, "endpoint_engine/partitions.json"));

    static const uint8_t bad_bytecode[] = {0x00, 0x01, 0x02, 0x03};
    ByteCursor bytecode = ByteCursorFromArray(bad_bytecode, sizeof(bad_bytecode));
    ByteCursor partitions = ByteCursorFromByteBuf(partitions_buf);

    Endpoints::BddEngine engine(bytecode, partitions, allocator);
    ByteBufDelete(partitions_buf);
    ASSERT_FALSE(engine);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BddEngineDefaultAllocator, s_TestBddEngineDefaultAllocator)
static int s_TestBddEngineDefaultAllocator(Allocator *allocator, void *ctx)
{
    (void)ctx;
    (void)allocator;
    ApiHandle apiHandle;

    ByteBuf bytecode_buf;
    ASSERT_TRUE(ByteBufInitFromFile(bytecode_buf, ApiAllocator(), "endpoint_engine/bdd_ruleset.bin"));

    ByteBuf partitions_buf;
    ASSERT_TRUE(ByteBufInitFromFile(partitions_buf, ApiAllocator(), "endpoint_engine/partitions.json"));

    /* No allocator passed — uses ApiAllocator() default */
    Endpoints::BddEngine engine(ByteCursorFromByteBuf(bytecode_buf), ByteCursorFromByteBuf(partitions_buf));
    ASSERT_TRUE(engine);

    Endpoints::RequestContext context;
    context.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2"));
    context.AddString(ByteCursorFromCString("Bucket"), ByteCursorFromCString("bucket-name"));

    auto resolved = engine.Resolve(context);
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(resolved->IsEndpoint());
    ASSERT_TRUE(resolved->GetUrl()->compare("https://bucket-name.s3.us-west-2.amazonaws.com") == 0);

    ByteBufDelete(bytecode_buf);
    ByteBufDelete(partitions_buf);
    return AWS_OP_SUCCESS;
}
