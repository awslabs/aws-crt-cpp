#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

namespace Aws
{
    namespace Crt
    {
        /**
         * Custom implementation of an in_place type tag for constructor parameter list
         */
        struct InPlaceT
        {
            explicit InPlaceT() = default;
        };
        static constexpr InPlaceT InPlace{};

    } // namespace Crt
} // namespace Aws
