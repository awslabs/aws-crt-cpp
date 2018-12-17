/*
 * Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
#include <aws/crt/Api.h>
#include <aws/crt/JsonObject.h>
#include <aws/testing/aws_test_harness.h>

static int s_BasicJsonParsing(struct aws_allocator *allocator, void *)
{
    Aws::Crt::ApiHandle apiHandle(allocator);

    const Aws::Crt::String jsonValue =
            "{\"testStringKey\":\"testStringValue\", \"testIntKey\":10, "
            "\"testBoolKey\":false, \"array\": [\"stringArrayEntry1\", \"stringArrayEntry2\"], "
            "\"object\": {\"testObjectStringKey\":\"testObjectStringValue\"}}";

    Aws::Crt::JsonObject value(jsonValue);
    ASSERT_TRUE(value.WasParseSuccessful());
    auto view = value.View();
    ASSERT_TRUE(value.GetErrorMessage().empty());
    ASSERT_TRUE("testStringValue" == view.GetString("testStringKey"));
    ASSERT_INT_EQUALS(10, view.GetInteger("testIntKey"));
    ASSERT_FALSE(view.GetBool("testBoolKey"));
    ASSERT_TRUE(view.GetJsonObject("object").AsString().empty());
    ASSERT_TRUE("stringArrayEntry1" == view.GetArray("array")[0].AsString());
    ASSERT_TRUE("stringArrayEntry2" == view.GetArray("array")[1].AsString());
    ASSERT_TRUE("testObjectStringValue" == view.GetJsonObject("object").GetString("testObjectStringKey"));

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(BasicJsonParsing, s_BasicJsonParsing)
