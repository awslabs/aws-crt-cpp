/*! \cond DOXYGEN_PRIVATE
** Hide API from this file in doxygen. Set DOXYGEN_PRIVATE in doxygen
** config to enable this file for doxygen.
*/
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
        namespace Mqtt
        {
            /**
             * @internal
             * IoT Device SDK Metrics Structure
             */
            struct IoTDeviceSDKMetrics
            {
                String LibraryName;

                IoTDeviceSDKMetrics() { LibraryName = "IoTDeviceSDK/CPP"; }

                void initializeRawOptions(aws_mqtt_iot_metrics &raw_options) noexcept
                {
                    raw_options.library_name = ByteCursorFromString(LibraryName);
                }
            };
        } // namespace Mqtt
    } // namespace Crt
} // namespace Aws

/*! \endcond */
