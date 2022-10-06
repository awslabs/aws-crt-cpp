/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
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

    ByteCursor cur = ByteCursorFromCString(sample_ruleset);
    Aws::Crt::Endpoints::RuleEngine engine(cur, allocator);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(RuleEngine, s_TestRuleEngine)
