/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/endpoints/BddEngine.h>
#include <aws/testing/aws_test_harness.h>

using namespace Aws::Crt;

static int s_RunResolve(
    Allocator *allocator,
    Endpoints::RequestContext &context,
    Optional<Endpoints::ResolutionOutcome> &out_resolved)
{
    ByteBuf partitions_buf;
    ASSERT_TRUE(ByteBufInitFromFile(partitions_buf, allocator, "sample_partitions.json"));
    ByteCursor partitions = ByteCursorFromByteBuf(partitions_buf);

    Endpoints::BddEngine engine(allocator, "bdd/endpoint-bdd-encoded.bin", partitions);
    ByteBufDelete(partitions_buf);
    ASSERT_TRUE(engine);

    out_resolved = engine.Resolve(context);
    ASSERT_TRUE(out_resolved.has_value());
    ASSERT_TRUE(out_resolved->IsEndpoint());

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BddEngineVirtual, s_TestBddEngineVirtual)
static int s_TestBddEngineVirtual(Allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::Endpoints::RequestContext context(allocator);
    context.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2"));
    context.AddString(ByteCursorFromCString("Bucket"), ByteCursorFromCString("bucket-name"));

    Aws::Crt::Optional<Aws::Crt::Endpoints::ResolutionOutcome> resolved;
    ASSERT_SUCCESS(s_RunResolve(allocator, context, resolved));
    ASSERT_TRUE(resolved->GetUrl()->compare("https://bucket-name.s3.us-west-2.amazonaws.com") == 0);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BddEnginePath, s_TestBddEnginePath)
static int s_TestBddEnginePath(Allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::Endpoints::RequestContext context(allocator);
    context.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2"));
    context.AddBoolean(ByteCursorFromCString("ForcePathStyle"), true);
    context.AddString(ByteCursorFromCString("Bucket"), ByteCursorFromCString("bucket-name"));

    Aws::Crt::Optional<Aws::Crt::Endpoints::ResolutionOutcome> resolved;
    ASSERT_SUCCESS(s_RunResolve(allocator, context, resolved));
    ASSERT_TRUE(resolved->GetUrl()->compare("https://s3.us-west-2.amazonaws.com/bucket-name") == 0);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BddEngineDataplaneZone, s_TestBddEngineDataplaneZone)
static int s_TestBddEngineDataplaneZone(Allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::Endpoints::RequestContext context(allocator);
    context.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-east-1"));
    context.AddString(ByteCursorFromCString("Bucket"), ByteCursorFromCString("mybucket--abcd-ab1--x-s3"));

    Aws::Crt::Optional<Aws::Crt::Endpoints::ResolutionOutcome> resolved;
    ASSERT_SUCCESS(s_RunResolve(allocator, context, resolved));
    ASSERT_TRUE(
        resolved->GetUrl()->compare("https://mybucket--abcd-ab1--x-s3.s3express-abcd-ab1.us-east-1.amazonaws.com") ==
        0);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BddEngineAccessPoint, s_TestBddEngineAccessPoint)
static int s_TestBddEngineAccessPoint(Allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::Endpoints::RequestContext context(allocator);
    context.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2"));
    context.AddString(
        ByteCursorFromCString("Bucket"),
        ByteCursorFromCString("arn:aws:s3:us-west-2:123456789012:accesspoint:myendpoint"));

    Aws::Crt::Optional<Aws::Crt::Endpoints::ResolutionOutcome> resolved;
    ASSERT_SUCCESS(s_RunResolve(allocator, context, resolved));
    ASSERT_TRUE(
        resolved->GetUrl()->compare("https://myendpoint-123456789012.s3-accesspoint.us-west-2.amazonaws.com") == 0);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BddEngineOutpost, s_TestBddEngineOutpost)
static int s_TestBddEngineOutpost(Allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::Endpoints::RequestContext context(allocator);
    context.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2"));
    context.AddString(
        ByteCursorFromCString("Bucket"),
        ByteCursorFromCString(
            "arn:aws:s3-outposts:us-west-2:123456789012:outpost/op-01234567890123456/accesspoint/reports"));

    Aws::Crt::Optional<Aws::Crt::Endpoints::ResolutionOutcome> resolved;
    ASSERT_SUCCESS(s_RunResolve(allocator, context, resolved));
    ASSERT_TRUE(
        resolved->GetUrl()->compare(
            "https://reports-123456789012.op-01234567890123456.s3-outposts.us-west-2.amazonaws.com") == 0);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BddEngineInvalidBytecode, s_TestBddEngineInvalidBytecode)
static int s_TestBddEngineInvalidBytecode(Allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    ByteBuf partitions_buf;
    ASSERT_TRUE(ByteBufInitFromFile(partitions_buf, allocator, "sample_partitions.json"));

    static const uint8_t bad_bytecode[] = {0x00, 0x01, 0x02, 0x03};
    ByteCursor bytecode = ByteCursorFromArray(bad_bytecode, sizeof(bad_bytecode));
    ByteCursor partitions = ByteCursorFromByteBuf(partitions_buf);

    Aws::Crt::Endpoints::BddEngine engine(allocator, bytecode, partitions);
    ByteBufDelete(partitions_buf);
    ASSERT_FALSE(engine);

    return AWS_OP_SUCCESS;
}
