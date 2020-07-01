/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/JsonObject.h>
#include <aws/testing/aws_test_harness.h>

static int s_BasicJsonParsing(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
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

static int s_JsonNullParseTest(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    const Aws::Crt::String jsonValue = "{\"testStringKey\":null,\"testIntKey\":10,"
                                       "\"array\":[null,\"stringArrayEntry\"],"
                                       "\"object\":{\"testObjectStringKey\":null}}";

    Aws::Crt::JsonObject value(jsonValue);
    ASSERT_TRUE(value.WasParseSuccessful());

    auto str = value.View().WriteCompact(true);
    ASSERT_STR_EQUALS(jsonValue.c_str(), str.c_str());
    str = value.View().WriteCompact(false);
    ASSERT_STR_EQUALS(jsonValue.c_str(), str.c_str());

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(JsonNullParsing, s_JsonNullParseTest)

static int s_JsonNullNestedObjectTest(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    const Aws::Crt::String jsonValue = "{\"testStringKey\":null,\"testIntKey\":10,"
                                       "\"array\":[null,\"stringArrayEntry\"],"
                                       "\"object\":{\"testObjectStringKey\":null}}";

    Aws::Crt::JsonObject value(jsonValue);
    ASSERT_TRUE(value.WasParseSuccessful());

    Aws::Crt::JsonObject doc;
    doc.WithObject("null_members", jsonValue);

    const Aws::Crt::String expectedValue = "{\"null_members\":{\"testStringKey\":null,\"testIntKey\":10,"
                                           "\"array\":[null,\"stringArrayEntry\"],"
                                           "\"object\":{\"testObjectStringKey\":null}}}";
    auto str = doc.View().WriteCompact(true);
    ASSERT_STR_EQUALS(expectedValue.c_str(), str.c_str());
    str = doc.View().WriteCompact(false);
    ASSERT_STR_EQUALS(expectedValue.c_str(), str.c_str());

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(JsonNullNestedObject, s_JsonNullNestedObjectTest)

static int s_JsonExplicitNullTest(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    const Aws::Crt::String expectedValue = "{\"testKey\":null}";

    Aws::Crt::JsonObject doc;
    Aws::Crt::JsonObject nullObject;
    nullObject.AsNull();

    doc.WithObject("testKey", nullObject);

    auto str = doc.View().WriteCompact(true);
    ASSERT_STR_EQUALS(expectedValue.c_str(), str.c_str());
    str = doc.View().WriteCompact(false);
    ASSERT_STR_EQUALS(expectedValue.c_str(), str.c_str());

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(JsonExplicitNull, s_JsonExplicitNullTest)
