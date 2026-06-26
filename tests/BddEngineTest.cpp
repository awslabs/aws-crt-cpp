/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/endpoints/BddEngine.h>
#include <aws/testing/aws_test_harness.h>

using namespace Aws::Crt;

/*
 * Compiled bytecode for a simple S3-like ruleset with two parameters:
 *   Region (string, required)
 *   UseAccelerate (boolean, optional, default false)
 *
 * Rules:
 *   Region == "us-east-1" -> https://s3.us-east-1.amazonaws.com
 *   otherwise             -> error "Invalid region"
 */
// clang-format off
static const uint8_t s_simple_bdd_bytecode[] = {
    0x52, 0x44, 0x50, 0x45, 0x7f, 0x00, 0x00, 0x00, 0x31, 0x2e, 0x31, 0x24, 0x24, 0x52, 0x65, 0x67,
    0x69, 0x6f, 0x6e, 0x24, 0x24, 0x55, 0x73, 0x65, 0x41, 0x63, 0x63, 0x65, 0x6c, 0x65, 0x72, 0x61,
    0x74, 0x65, 0x24, 0x24, 0x75, 0x73, 0x2d, 0x65, 0x61, 0x73, 0x74, 0x2d, 0x31, 0x24, 0x24, 0x68,
    0x74, 0x74, 0x70, 0x73, 0x3a, 0x2f, 0x2f, 0x73, 0x33, 0x2e, 0x75, 0x73, 0x2d, 0x65, 0x61, 0x73,
    0x74, 0x2d, 0x31, 0x2e, 0x61, 0x6d, 0x61, 0x7a, 0x6f, 0x6e, 0x61, 0x77, 0x73, 0x2e, 0x63, 0x6f,
    0x6d, 0x24, 0x24, 0x7b, 0x22, 0x61, 0x75, 0x74, 0x68, 0x53, 0x63, 0x68, 0x65, 0x6d, 0x65, 0x73,
    0x22, 0x3a, 0x5b, 0x7b, 0x22, 0x6e, 0x61, 0x6d, 0x65, 0x22, 0x3a, 0x22, 0x73, 0x69, 0x67, 0x76,
    0x34, 0x22, 0x7d, 0x5d, 0x7d, 0x24, 0x24, 0x49, 0x6e, 0x76, 0x61, 0x6c, 0x69, 0x64, 0x20, 0x72,
    0x65, 0x67, 0x69, 0x6f, 0x6e, 0x24, 0x24, 0x00, 0x00, 0x03, 0x00, 0x02, 0x00, 0x01, 0x05, 0x00,
    0x06, 0x00, 0x00, 0x01, 0x00, 0x02, 0x0d, 0x00, 0x0d, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00,
    0x10, 0x00, 0x01, 0x00, 0x04, 0x05, 0x00, 0x06, 0x00, 0x00, 0x10, 0x04, 0x02, 0x00, 0x04, 0x05,
    0x00, 0x06, 0x00, 0x01, 0x1c, 0x00, 0x09, 0x00, 0x00, 0x02, 0x00, 0x20, 0x01, 0x27, 0x00, 0x22,
    0x00, 0x4b, 0x00, 0x22, 0x00, 0x00, 0x00, 0x21, 0x01, 0x6f, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x03, 0x00, 0x00, 0x00, 0x24, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
};
// clang-format on

const char s_sample_partitions[] = R"({
    "version": "1.1",
    "partitions": [
      {
        "id": "aws",
        "regionRegex": "^(us|eu|ap|sa|ca|me|af)\\-\\w+\\-\\d+$",
        "regions": {
          "us-east-1": {},
          "us-east-2": {},
          "us-west-1": {},
          "us-west-2": {}
        },
        "outputs": {
          "name": "aws",
          "dnsSuffix": "amazonaws.com",
          "dualStackDnsSuffix": "api.aws",
          "supportsFIPS": true,
          "supportsDualStack": true
        }
      }
    ]
  })";

static int s_TestBddEngineResolveEndpoint(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;

    Aws::Crt::ApiHandle apiHandle(allocator);

    ByteCursor bytecode =
        ByteCursorFromArray(s_simple_bdd_bytecode, sizeof(s_simple_bdd_bytecode));
    ByteCursor partitions = ByteCursorFromCString(s_sample_partitions);

    Aws::Crt::Endpoints::BddEngine engine(bytecode, partitions, allocator);
    ASSERT_TRUE(engine);

    Aws::Crt::Endpoints::RequestContext context(allocator);
    context.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-east-1"));

    auto resolved = engine.Resolve(context);
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(resolved->IsEndpoint());
    ASSERT_TRUE(resolved->GetUrl().has_value());
    ASSERT_TRUE(resolved->GetUrl()->compare("https://s3.us-east-1.amazonaws.com") == 0);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BddEngineResolveEndpoint, s_TestBddEngineResolveEndpoint)

static int s_TestBddEngineResolveError(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;

    Aws::Crt::ApiHandle apiHandle(allocator);

    ByteCursor bytecode =
        ByteCursorFromArray(s_simple_bdd_bytecode, sizeof(s_simple_bdd_bytecode));
    ByteCursor partitions = ByteCursorFromCString(s_sample_partitions);

    Aws::Crt::Endpoints::BddEngine engine(bytecode, partitions, allocator);
    ASSERT_TRUE(engine);

    Aws::Crt::Endpoints::RequestContext context(allocator);
    context.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2"));

    auto resolved = engine.Resolve(context);
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(resolved->IsError());
    ASSERT_TRUE(resolved->GetError().has_value());
    ASSERT_TRUE(resolved->GetError()->compare("Invalid region") == 0);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BddEngineResolveError, s_TestBddEngineResolveError)

static int s_TestBddEngineInvalidBytecode(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;

    Aws::Crt::ApiHandle apiHandle(allocator);

    static const uint8_t bad_bytecode[] = {0x00, 0x01, 0x02, 0x03};
    ByteCursor bytecode = ByteCursorFromArray(bad_bytecode, sizeof(bad_bytecode));
    ByteCursor partitions = ByteCursorFromCString(s_sample_partitions);

    Aws::Crt::Endpoints::BddEngine engine(bytecode, partitions, allocator);
    ASSERT_FALSE(engine);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BddEngineInvalidBytecode, s_TestBddEngineInvalidBytecode)
