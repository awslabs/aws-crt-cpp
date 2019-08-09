/*
 * Copyright 2010-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
#include <aws/crt/ByteBuf.h>

#include <aws/crt/http/HttpRequestResponse.h>

#include <aws/http/request_response.h>

#include <aws/testing/aws_test_harness.h>

#include <sstream>

using namespace Aws::Crt::Http;

static int s_HttpRequestTestCreateDestroy(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        Aws::Crt::Http::HttpRequest request(allocator);
        request.SetMethod(Aws::Crt::ByteCursor("GET"));
        request.SetPath(Aws::Crt::ByteCursor("/index"));

        std::shared_ptr<Aws::Crt::Io::IStream> stream = std::make_shared<std::stringstream>("TestContent");
        request.SetBody(stream);

        std::shared_ptr<Aws::Crt::Io::IStream> stream2 = std::make_shared<std::stringstream>("SomeOtherContent");
        request.SetBody(stream2);

        HttpHeader header1 = {aws_byte_cursor_from_c_str("Host"), aws_byte_cursor_from_c_str("www.test.com")};
        request.AddHeader(header1);

        HttpHeader header2 = {aws_byte_cursor_from_c_str("Authorization"), aws_byte_cursor_from_c_str("sadf")};
        request.AddHeader(header2);

        HttpHeader header3 = {aws_byte_cursor_from_c_str("UserAgent"), aws_byte_cursor_from_c_str("unit-tests-1.0")};
        request.AddHeader(header3);

        request.EraseHeader(2);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(HttpRequestTestCreateDestroy, s_HttpRequestTestCreateDestroy)
