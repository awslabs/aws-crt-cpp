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
            class AWS_CRT_CPP_API AWSIoTMetrics
            {
              public:
                AWSIoTMetrics();
                ~AWSIoTMetrics() = default;
                AWSIoTMetrics(const AWSIoTMetrics &other);
                AWSIoTMetrics &operator=(const AWSIoTMetrics &other);
                AWSIoTMetrics(AWSIoTMetrics &&other) noexcept;
                AWSIoTMetrics &operator=(AWSIoTMetrics &&other) noexcept;

                /**
                 * Sets the library name identifier.
                 *
                 * @param name The library name
                 */
                void SetLibraryName(Aws::Crt::String name);

                /**
                 * Returns a const reference to the library name.
                 */
                const Aws::Crt::String &GetLibraryName() const;

                /**
                 * Sets or updates a single metadata entry.
                 *
                 * @param key The metadata key.
                 * @param value The metadata value.
                 */
                void SetMetadataEntry(const Crt::String &key, const Crt::String &value);

                /**
                 * Removes a single metadata entry by key.
                 * No-op if the key does not exist.
                 *
                 * @param key The metadata key to remove.
                 */
                void RemoveMetadataEntry(const Crt::String &key);

                /**
                 * Returns a const reference to the metadata map.
                 */
                const Crt::Map<Crt::String, Crt::String> &GetMetadata() const;

              private:
                friend class Mqtt5::Mqtt5ClientOptions;
                friend class MqttConnectionCore;
                friend class IoTSDKMetricsEncoder;

                /**
                 * The library name identifier.
                 * Maps to the SDK attribute in the username field.
                 */
                Aws::Crt::String m_libraryName;

                /**
                 * Metadata key-value pairs to include in the Metadata field of the username.
                 */
                Crt::Map<Crt::String, Crt::String> m_metadata;

                /**
                 * Populates a raw aws_mqtt_iot_metrics struct from this object.
                 *
                 * The byte cursors in the output struct point into the strings owned by
                 * this AWSIoTMetrics instance. Do not modify m_libraryName or m_metadata
                 * after calling this method while the output struct is still in use.
                 *
                 * @param raw_options Output C struct to populate.
                 */
                void initializeRawOptions(struct aws_mqtt_iot_metrics &raw_options) noexcept;

                /**
                 * Resets the raw metadata entry cache.
                 * Must be called whenever m_libraryName or m_metadata is modified.
                 */
                void resetRawData() noexcept;

                /**
                 * Storage for the raw C metadata entry array.
                 * Byte cursors in these entries point into the strings in m_metadata.
                 * Must not be modified after initializeRawOptions() is called while the
                 * corresponding aws_mqtt_iot_metrics struct is still in use.
                 */
                Crt::Vector<aws_mqtt_metadata_entry> m_rawMetadataEntries;
            };
        } // namespace Mqtt
    } // namespace Crt
} // namespace Aws
