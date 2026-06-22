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
        namespace Mqtt5
        {
            class Mqtt5ClientOptions;
        } // namespace Mqtt5

        namespace Mqtt
        {
            /**
             * IoT Device SDK Metrics Structure.
             *
             * Holds the library name and a list of metadata key-value pairs to be appended
             * to the MQTT CONNECT packet's username field.
             */
            struct AWS_CRT_CPP_API IoTDeviceSDKMetrics
            {
                /**
                 * The library name identifier (default: "IoTDeviceSDK/CPP").
                 * Maps to the SDK attribute in the username field.
                 */
                Aws::Crt::String LibraryName;

                /**
                 * Metadata key-value pairs to include in the Metadata field of the username.
                 */
                Crt::Vector<std::pair<Crt::String, Crt::String>> Metadata;

                IoTDeviceSDKMetrics() { LibraryName = "IoTDeviceSDK/CPP"; }

                /**
                 * Adds or updates a metadata entry.
                 * If a key already exists, its value is overwritten.
                 *
                 * @param key   Metadata key
                 * @param value Metadata value
                 */
                void AddMetadata(const Crt::String &key, const Crt::String &value) noexcept;

                friend class Mqtt5::Mqtt5ClientOptions;
                friend class MqttConnectionCore;

              private:
                /**
                 * Populates a raw aws_mqtt_iot_metrics struct from this object.
                 *
                 * The byte cursors in the output struct point into the strings owned by
                 * this IoTDeviceSDKMetrics instance. Do not modify LibraryName or Metadata
                 * after calling this method while the output struct is still in use.
                 *
                 * @param raw_options Output C struct to populate.
                 */
                void initializeRawOptions(struct aws_mqtt_iot_metrics &raw_options) noexcept;

                /**
                 * Storage for the raw C metadata entry array.
                 * Byte cursors in these entries point into the strings in Metadata.
                 * Must not be modified after initializeRawOptions() is called while the
                 * corresponding aws_mqtt_iot_metrics struct is still in use.
                 */
                Crt::Vector<aws_mqtt_metadata_entry> m_rawMetadataEntries;
            };
        } // namespace Mqtt
    } // namespace Crt
} // namespace Aws
