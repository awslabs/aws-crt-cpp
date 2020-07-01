/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/Optional.h>
#include <aws/testing/aws_test_harness.h>

const char *s_test_str = "This is a string, that should be long enough to avoid small string optimizations";

static int s_OptionalCopySafety(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::Optional<Aws::Crt::String> str1(s_test_str);
    Aws::Crt::Optional<Aws::Crt::String> strCpyAssigned = str1;
    Aws::Crt::Optional<Aws::Crt::String> strCpyConstructedOptional(strCpyAssigned);
    Aws::Crt::Optional<Aws::Crt::String> strCpyConstructedValue(*strCpyAssigned);

    // now force data access just to check there's not a segfault hiding somewhere.
    ASSERT_STR_EQUALS(s_test_str, str1->c_str());
    ASSERT_STR_EQUALS(s_test_str, strCpyAssigned->c_str());
    ASSERT_STR_EQUALS(s_test_str, strCpyConstructedOptional->c_str());
    ASSERT_STR_EQUALS(s_test_str, strCpyConstructedValue->c_str());

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(OptionalCopySafety, s_OptionalCopySafety)

static int s_OptionalMoveSafety(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::Optional<Aws::Crt::String> str1(s_test_str);
    Aws::Crt::Optional<Aws::Crt::String> strMoveAssigned = std::move(str1);
    ASSERT_STR_EQUALS(s_test_str, strMoveAssigned->c_str());

    Aws::Crt::Optional<Aws::Crt::String> strMoveValueAssigned = std::move(*strMoveAssigned);
    ASSERT_STR_EQUALS(s_test_str, strMoveValueAssigned->c_str());

    Aws::Crt::Optional<Aws::Crt::String> strMoveConstructed(std::move(strMoveValueAssigned));
    ASSERT_STR_EQUALS(s_test_str, strMoveConstructed->c_str());

    Aws::Crt::Optional<Aws::Crt::String> strMoveValueConstructed(std::move(*strMoveConstructed));
    ASSERT_STR_EQUALS(s_test_str, strMoveValueConstructed->c_str());

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(OptionalMoveSafety, s_OptionalMoveSafety)

class CopyMoveTester
{
  public:
    CopyMoveTester() : m_copied(false), m_moved(false) {}
    CopyMoveTester(const CopyMoveTester &other) : m_copied(true), m_moved(false) {}
    CopyMoveTester(CopyMoveTester &&other) : m_copied(false), m_moved(true) {}

    CopyMoveTester &operator=(const CopyMoveTester &other)
    {
        m_copied = true;
        m_moved = false;
        return *this;
    }
    CopyMoveTester &operator=(CopyMoveTester &&other)
    {
        m_copied = false;
        m_moved = true;
        return *this;
    }

    ~CopyMoveTester() {}

    bool m_copied;
    bool m_moved;
};

static int s_OptionalCopyAndMoveSemantics(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    CopyMoveTester initialItem;
    ASSERT_FALSE(initialItem.m_copied);
    ASSERT_FALSE(initialItem.m_moved);

    Aws::Crt::Optional<CopyMoveTester> copyConstructedValue(initialItem);
    ASSERT_TRUE(copyConstructedValue->m_copied);
    ASSERT_FALSE(copyConstructedValue->m_moved);

    Aws::Crt::Optional<CopyMoveTester> copyConstructedOptional(copyConstructedValue);
    ASSERT_TRUE(copyConstructedOptional->m_copied);
    ASSERT_FALSE(copyConstructedOptional->m_moved);

    Aws::Crt::Optional<CopyMoveTester> copyAssignedValue = initialItem;
    ASSERT_TRUE(copyAssignedValue->m_copied);
    ASSERT_FALSE(copyAssignedValue->m_moved);

    Aws::Crt::Optional<CopyMoveTester> copyAssignedOptional = copyConstructedOptional;
    ASSERT_TRUE(copyAssignedOptional->m_copied);
    ASSERT_FALSE(copyAssignedOptional->m_moved);

    Aws::Crt::Optional<CopyMoveTester> moveConstructedValue(std::move(initialItem));
    ASSERT_FALSE(moveConstructedValue->m_copied);
    ASSERT_TRUE(moveConstructedValue->m_moved);

    Aws::Crt::Optional<CopyMoveTester> moveConstructedOptional(std::move(moveConstructedValue));
    ASSERT_FALSE(moveConstructedOptional->m_copied);
    ASSERT_TRUE(moveConstructedOptional->m_moved);

    Aws::Crt::Optional<CopyMoveTester> moveAssignedValue = std::move(*moveConstructedOptional);
    ASSERT_FALSE(moveAssignedValue->m_copied);
    ASSERT_TRUE(moveAssignedValue->m_moved);

    Aws::Crt::Optional<CopyMoveTester> moveAssignedOptional = std::move(moveAssignedValue);
    ASSERT_FALSE(moveAssignedOptional->m_copied);
    ASSERT_TRUE(moveAssignedOptional->m_moved);

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(OptionalCopyAndMoveSemantics, s_OptionalCopyAndMoveSemantics)
