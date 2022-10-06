/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/endpoints/RuleEngine.h>
#include <aws/testing/aws_test_harness.h>

using namespace Aws::Crt;

const char sample_ruleset[] = R"({
          "version": "1.0",
          "serviceId": "example",
          "parameters": {
            "Region": {
              "type": "string",
              "builtIn": "AWS::Region",
              "documentation": "The region to dispatch the request to"
            }
          },
          "rules": [
            {
              "documentation": "rules for when region isSet",
              "type": "tree",
              "conditions": [
                {
                  "fn": "isSet",
                  "argv": [
                    {
                      "ref": "Region"
                    }
                  ]
                }
              ],
              "rules": [
                {
                  "type": "endpoint",
                  "conditions": [
                    {
                      "fn": "aws.partition",
                      "argv": [
                        {
                          "ref": "Region"
                        }
                      ],
                      "assign": "partitionResult"
                    }
                  ],
                  "endpoint": {
                    "url": "https://example.{Region}.{partitionResult#dnsSuffix}"
                  }
                },
                {
                  "type": "error",
                  "documentation": "invalid region value",
                  "conditions": [],
                  "error": "unable to determine endpoint for region: {Region}"
                }
              ]
            },
            {
              "type": "endpoint",
              "documentation": "the single service global endpoint",
              "conditions": [],
              "endpoint": {
                "url": "https://example.amazonaws.com"
              }
            }
          ]
        })";

static int s_TestRuleEngine(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;

    Aws::Crt::ApiHandle apiHandle(allocator);

    ByteCursor cur = ByteCursorFromCString(sample_ruleset);
    Aws::Crt::Endpoints::RuleEngine engine(cur, allocator);

    Aws::Crt::Endpoints::RequestContext context(allocator);
    context.AddString(ByteCursorFromCString("Region"), ByteCursorFromCString("us-west-2"));

    auto resolved = engine.resolve(context);
    ASSERT_TRUE(resolved.has_value());
    ASSERT_TRUE(resolved->IsEndpoint());

    ASSERT_TRUE(resolved->getUrl()->compare("https://example.us-west-2.amazonaws.com") == 0);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(RuleEngine, s_TestRuleEngine)
