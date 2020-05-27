
/*
 * Copyright 2010-2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
#include <aws/crt/UUID.h>

#include <aws/testing/aws_test_harness.h>
#include <iostream>
#include <utility>

static int s_UUIDToString(Aws::Crt::Allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);
    Aws::Crt::UUID Uuid;
    Aws::Crt::String uuidStr = Uuid.ToString();
    ASSERT_TRUE(uuidStr.length() != 0);
    ASSERT_TRUE(Uuid == Aws::Crt::UUID(uuidStr));

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(UUIDToString, s_UUIDToString)
