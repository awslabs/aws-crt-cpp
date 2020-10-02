#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Types.h>

namespace Aws
{
    namespace Crt
    {
        /**
         * Method for tests to wait for all outstanding thread-based resources to fully destruct.  Must be called
         * before a memory check.  We don't want this to be a part of normal (ApiHandle) destruction.
         */
        void TestCleanupAndWait();
    } // namespace Crt
} // namespace Aws
