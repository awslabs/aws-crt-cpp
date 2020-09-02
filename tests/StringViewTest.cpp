/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/StringView.h>
#include <aws/crt/Types.h>
#include <aws/testing/aws_test_harness.h>

static int s_test_string_view(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        const char *data = "abc123xyz";

        Aws::Crt::StringView sv(data);
        ASSERT_INT_EQUALS(9u, sv.size());
        ASSERT_FALSE(sv.empty());
        ASSERT_PTR_EQUALS(data, sv.data());
        ASSERT_TRUE(*(sv.begin()) == 'a');
        ASSERT_TRUE(*(sv.cbegin()) == 'a');
        ASSERT_TRUE(*(sv.rbegin()) == 'z');
        ASSERT_TRUE(*(sv.crbegin()) == 'z');
        ASSERT_TRUE(sv[0] == 'a');
        ASSERT_TRUE(sv[3] == '1');
        ASSERT_TRUE(sv[6] == 'x');
        ASSERT_TRUE(sv[8] == 'z');
        ASSERT_TRUE(sv.front() == 'a');
        ASSERT_TRUE(sv.back() == 'z');

        auto subsv = sv.substr(3, 4);
        ASSERT_INT_EQUALS(4u, subsv.size());
        ASSERT_PTR_EQUALS(data + 3, subsv.data());
        ASSERT_TRUE(subsv.front() == '1');
        ASSERT_TRUE(subsv.back() == 'x');

        sv.remove_prefix(3);
        ASSERT_PTR_EQUALS(data + 3, sv.data());
        ASSERT_INT_EQUALS(6u, sv.size());
        ASSERT_TRUE(sv.front() == '1');

        sv.remove_suffix(3);
        ASSERT_PTR_EQUALS(data + 3, sv.data());
        ASSERT_INT_EQUALS(3u, sv.size());
        ASSERT_TRUE(sv.front() == '1');
        ASSERT_TRUE(sv.back() == '3');

        const char *data1 = "123456789";
        Aws::Crt::StringView sv1(data1);

        sv.swap(sv1);
        ASSERT_PTR_EQUALS(data1, sv.data());
        ASSERT_INT_EQUALS(9u, sv.size());

        ASSERT_PTR_EQUALS(data + 3, sv1.data());
        ASSERT_INT_EQUALS(3u, sv1.size());
    }

    return 0;
}

AWS_TEST_CASE(StringViewTest, s_test_string_view)
