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

static int s_TestBddEngineInvalidBytecode(Allocator *allocator, void *ctx)
{
    (void)ctx;
    ApiHandle apiHandle(allocator);

    ByteBuf partitions;
    ASSERT_TRUE(ByteBufInitFromFile(partitions, allocator, "endpoint_engine/partitions.json"));

    static const uint8_t bad_bytecode[] = {0x00, 0x01, 0x02, 0x03};
    ByteCursor bytecode = ByteCursorFromArray(bad_bytecode, sizeof(bad_bytecode));

    Endpoints::BddEngine engine(bytecode, ByteCursorFromByteBuf(partitions), allocator);
    ByteBufDelete(partitions);
    ASSERT_FALSE(engine);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(BddEngineInvalidBytecode, s_TestBddEngineInvalidBytecode)

static int s_TestBddEngineDefaultAllocator(Allocator *allocator, void *ctx)
{
    (void)ctx;
    (void)allocator;
    ApiHandle apiHandle;

    ByteBuf bytecode, partitions;
    ASSERT_TRUE(ByteBufInitFromFile(bytecode, ApiAllocator(), "endpoint_engine/model.bin"));
    ASSERT_TRUE(ByteBufInitFromFile(partitions, ApiAllocator(), "endpoint_engine/partitions.json"));

    /* No allocator passed — uses ApiAllocator() default */
    Endpoints::BddEngine engine(ByteCursorFromByteBuf(bytecode), ByteCursorFromByteBuf(partitions));
    ASSERT_TRUE(engine);

    Endpoints::RequestContext context;
    context.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2"));

    auto resolved = engine.Resolve(context);
    ByteBufDelete(bytecode);
    ByteBufDelete(partitions);
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(resolved->IsEndpoint());
    ASSERT_TRUE(resolved->GetUrl()->compare("https://example.us-west-2.amazonaws.com") == 0);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(BddEngineDefaultAllocator, s_TestBddEngineDefaultAllocator)
