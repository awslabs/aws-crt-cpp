/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/Variant.h>
#include <aws/testing/aws_test_harness.h>

const char *s_variant_test_str = "This is a string, that should be long enough to avoid small string optimizations";

static int s_VariantBasicOperandsCompile(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        Aws::Crt::ApiHandle apiHandle(allocator);

        {
            using MyTestVariant1 = Aws::Crt::Variant<int, char, Aws::Crt::String>;
            MyTestVariant1 var1;
            var1 = var1;
            MyTestVariant1 var1CpyAssigned;
            var1CpyAssigned = var1;
            MyTestVariant1 var1CpyConstructedVariant(var1CpyAssigned);

            MyTestVariant1 var1a = Aws::Crt::String(s_variant_test_str);
            var1a = var1a;
            var1 = var1a;
            MyTestVariant1 var1aCpyAssigned;
            var1CpyAssigned = var1a;
            MyTestVariant1 var1aCpyConstructedVariant(var1aCpyAssigned);
        }

        {
            // jsut a different order or types
            using MyTestVariant2 = Aws::Crt::Variant<Aws::Crt::String, int, char>;
            MyTestVariant2 var2;
            var2 = var2;
            MyTestVariant2 var2CpyAssigned;
            var2CpyAssigned = var2;
            MyTestVariant2 var2CpyConstructedVariant(var2CpyAssigned);

            MyTestVariant2 var2a = Aws::Crt::String(s_variant_test_str);
            var2a = var2a;
            var2 = var2a;
            MyTestVariant2 var2aCpyAssigned;
            var2CpyAssigned = var2a;
            MyTestVariant2 var2aCpyConstructedVariant(var2aCpyAssigned);
        }
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(VariantCompiles, s_VariantBasicOperandsCompile)


