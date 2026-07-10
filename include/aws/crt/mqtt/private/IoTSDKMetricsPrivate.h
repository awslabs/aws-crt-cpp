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
#include <aws/crt/io/TlsOptions.h>
#include <aws/crt/io/private/CertificateSource.h>
#include <aws/crt/mqtt/IoTSDKMetrics.h>
#include <aws/crt/mqtt/Mqtt5Client.h>
#include <aws/io/tls_channel_handler.h>

// Private constants and encoder for IoT SDK metrics.
// Not part of the public API — include only from CRT implementation files.

namespace Aws
{
    namespace Crt
    {
        namespace Mqtt
        {
            // Forward declaration
            class MqttConnectionCore;

            // Feature ID Constants
            //
            // Single-char IDs used to encode feature usage in the metrics string.
            // IDs are assigned sequentially and are never reused to ensure
            // historical data consistency.
            namespace MetricsFeatureId
            {
                constexpr char RetryJitterMode = 'A';
                constexpr char SessionBehavior = 'B';
                constexpr char OfflineQueueBehavior = 'C';
                constexpr char OutboundTopicAliasBehavior = 'D';
                constexpr char InboundTopicAliasBehavior = 'E';
                constexpr char ProtocolVersion = 'F';
                constexpr char SocketImplementation = 'G';
                constexpr char HttpProxyType = 'H';
                constexpr char CertificateSource = 'I';
                constexpr char TlsCipherPreference = 'J';
                constexpr char MinimumTlsVersion = 'K';
            } // namespace MetricsFeatureId

            // The current version of the IoT SDK metrics feature encoding format.
            // Included in the IoTSDKMetricsVersion metadata field.
            // Increment this when the feature encoding format changes.
            constexpr int IoTSDKMetricsFeatureVersion = 1;

            // Feature Value Constants for protocol version
            namespace MetricsProtocolVersionValue
            {
                constexpr char Mqtt311 = '3';
                constexpr char Mqtt5 = '5';
            } // namespace MetricsProtocolVersionValue

            // Feature Value Constants for socket implementation
            namespace MetricsSocketImplementationValue
            {
                constexpr char Posix = 'A';
                constexpr char Winsock = 'B';
                constexpr char AppleNetworkFramework = 'C';
            } // namespace MetricsSocketImplementationValue

            // Feature Value Constants for HTTP proxy type
            namespace MetricsHttpProxyTypeValue
            {
                constexpr char Http = 'A';
                constexpr char Https = 'B';
            } // namespace MetricsHttpProxyTypeValue

            // Feature Value Constants for certificate source
            namespace MetricsCertificateSourceValue
            {
                constexpr char CertificateFiles = 'A';
                constexpr char Pkcs11 = 'B';
                constexpr char WindowsCertStore = 'C';
                // 'D' is reserved for Java keystore (not applicable to C++)
                constexpr char Pkcs12File = 'E';
            } // namespace MetricsCertificateSourceValue

            // Feature Value Constants for TLS cipher preference
            // Assigned sequentially by aws_tls_cipher_pref enum value (1-11), skipping SYSTEM_DEFAULT (0).
            namespace MetricsTlsCipherPreferenceValue
            {
                constexpr char KmsPqTlsv10_2019_06 = 'A';     // enum 1, deprecated
                constexpr char KmsPqSikeTlsv10_2019_11 = 'B'; // enum 2, deprecated
                constexpr char KmsPqTlsv10_2020_02 = 'C';     // enum 3, deprecated
                constexpr char KmsPqSikeTlsv10_2020_02 = 'D'; // enum 4, deprecated
                constexpr char KmsPqTlsv10_2020_07 = 'E';     // enum 5, deprecated
                constexpr char PqTlsv10_2021_05 = 'F';        // enum 6, deprecated
                constexpr char PqTlsv12_2024_10 = 'G';        // enum 7
                constexpr char PqDefault = 'H';               // enum 8
                constexpr char Tlsv12_2025_07 = 'I';          // enum 9
                constexpr char Tlsv10_2023_06 = 'J';          // enum 10
                constexpr char NonPqDefault = 'K';            // enum 11
            } // namespace MetricsTlsCipherPreferenceValue

            // Feature Value Constants for minimum TLS version
            namespace MetricsMinimumTlsVersionValue
            {
                constexpr char SSLv3 = 'A';
                constexpr char TLSv1 = 'B';
                constexpr char TLSv1_1 = 'C';
                constexpr char TLSv1_2 = 'D';
                constexpr char TLSv1_3 = 'E';
            } // namespace MetricsMinimumTlsVersionValue

            /**
             * Encoder for IoT SDK metrics. Used to create the final AWSIoTMetrics directly from client options
             */
            class IoTSDKMetricsEncoder
            {
              public:
                /**
                 * Creates the final AWSIoTMetrics directly from Mqtt5ClientOptions.
                 * Reads features directly from the options.
                 *
                 * @param options The Mqtt5ClientOptions to extract features from.
                 * @return The final AWSIoTMetrics with all metadata set.
                 */
                static AWSIoTMetrics createMetricsForMqtt5(const Mqtt5::Mqtt5ClientOptions &options);

                /**
                 * Creates the final AWSIoTMetrics for an MQTT 3.1.1 connection.
                 *
                 * @param connectionCore The MqttConnectionCore to extract connection parameters from.
                 *
                 * @return The final AWSIoTMetrics with all metadata set.
                 */
                static AWSIoTMetrics createMetricsForMqtt311(const MqttConnectionCore &connectionCore);

              private:
                // Appends a "featureId/value" token to the feature list string.
                // Skips the token if value is '\0' (default — omit from list).
                static void appendFeature(Crt::String &featureList, char featureId, char value);

                /*
                 * This function create and update the final metrics from the userMetrics and crtFeatureList
                 *
                 * According to the following rules:
                 * - libraryName: set to default SDK Name. If the libraryName field is set from
                 *   user metrics, overwrite the default value.
                 * - Metadata - CRTVersion: not modifiable by user, automatically set to CRT version.
                 * - Metadata - IoTSDKMetricsVersion: If set by user metrics, validates whether the
                 *   metrics version matches the library's metrics version and processes IoTSDKFeature.
                 * - Metadata - IoTSDKFeature: merge the CRT feature and the input feature if the
                 *   metrics version matches.
                 * - Other user metadata: preserved in the output (excluding reserved keys).
                 */
                static AWSIoTMetrics createMetricsFromFeatureList(
                    const Crt::String &crtFeatureList,
                    const AWSIoTMetrics *userMetrics);

                /**
                 * Generates the encoded feature list string directly from Mqtt5ClientOptions.
                 * The format is ID/Value pairs separated by commas.
                 * Example: "A/B,C/A" means Feature A (retry_jitter_mode) with value B (FULL),
                 *          and Feature C (offline_queue_behavior) with value A (FAIL_NON_QOS1)
                 *
                 * @param options The Mqtt5ClientOptions to extract features from.
                 * @return The encoded feature list string.
                 */
                static Crt::String getEncodedFeatureListForMqtt5(const Mqtt5::Mqtt5ClientOptions &options);

                /**
                 * Generates the encoded feature list string for an MQTT 3.1.1 connection.
                 * Extracts proxy options and TLS options from the MqttConnectionCore.
                 *
                 * @param connectionCore The MqttConnectionCore to extract connection parameters from.
                 * @return The encoded feature list string.
                 */
                static Crt::String getEncodedFeatureListForMqtt311(const MqttConnectionCore &connectionCore);

                /**
                 * Merges CRT features with user-provided features.
                 * User features take precedence for the same feature ID.
                 * Feature list format: "A/B,C/D" where A,C are feature IDs and B,D are values.
                 *
                 * @param crtFeatures The CRT-generated feature list string.
                 * @param userFeatures The user-provided feature list string.
                 * @return The merged feature list string, sorted by feature ID.
                 */
                static Crt::String mergeFeatureLists(const Crt::String &crtFeatures, const Crt::String &userFeatures);

                // Extension mappings from existing enums to metrics values.
                // Returns '\0' for default/unset values (omit from the encoded feature list).

                // Maps ExponentialBackoffJitterMode to its metrics value char.
                static char metricsValueForRetryJitterMode(Mqtt5::ExponentialBackoffJitterMode mode);

                // Maps ClientSessionBehaviorType to its metrics value char.
                static char metricsValueForSessionBehavior(Mqtt5::ClientSessionBehaviorType behavior);

                // Maps ClientOperationQueueBehaviorType to its metrics value char.
                static char metricsValueForOfflineQueueBehavior(Mqtt5::ClientOperationQueueBehaviorType behavior);

                // Maps outbound topic alias behavior to its metrics value char.
                static char metricsValueForOutboundTopicAliasBehavior(Mqtt5::OutboundTopicAliasBehaviorType behavior);

                // Maps inbound topic alias behavior to its metrics value char.
                static char metricsValueForInboundTopicAliasBehavior(Mqtt5::InboundTopicAliasBehaviorType behavior);

                // Maps Io::CertificateSource to its metrics value char.
                // Returns '\0' for CertificateSource::None (omit from encoded list).
                static char metricsValueForCertificateSource(Io::CertificateSource source);

                // Maps aws_tls_cipher_pref to its metrics value char.
                // Returns '\0' for AWS_IO_TLS_CIPHER_PREF_SYSTEM_DEFAULT (omit from encoded list).
                static char metricsValueForTlsCipherPreference(aws_tls_cipher_pref pref);

                // Maps aws_tls_versions to its metrics value char.
                // Returns '\0' for AWS_IO_TLS_VER_SYS_DEFAULTS (omit from encoded list).
                static char metricsValueForMinimumTlsVersion(aws_tls_versions version);

                // Returns the metrics value char for the socket implementation on the current platform.
                // Detected at compile time: Winsock on Windows, Apple Network Framework on Apple, POSIX elsewhere.
                static char detectSocketImplementation();
            };

        } // namespace Mqtt
    } // namespace Crt
} // namespace Aws
/*! \endcond */
