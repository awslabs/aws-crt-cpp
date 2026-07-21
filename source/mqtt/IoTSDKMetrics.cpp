/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/mqtt/IoTSDKMetrics.h>
#include <aws/crt/mqtt/private/IoTSDKMetricsPrivate.h>
#include <aws/crt/mqtt/private/MqttConnectionCore.h>

#include <aws/crt/Config.h>
#include <aws/crt/io/private/TlsMetrics.h>

#include <cstdlib>

namespace Aws
{
    namespace Crt
    {
        namespace Mqtt
        {
            // File-scoped helper: returns true if the given character is a recognized MetricsFeatureId (A-K).
            static bool isValidFeatureId(char id)
            {
                return id == MetricsFeatureId::RetryJitterMode || id == MetricsFeatureId::SessionBehavior ||
                       id == MetricsFeatureId::OfflineQueueBehavior ||
                       id == MetricsFeatureId::OutboundTopicAliasBehavior ||
                       id == MetricsFeatureId::InboundTopicAliasBehavior || id == MetricsFeatureId::ProtocolVersion ||
                       id == MetricsFeatureId::SocketImplementation || id == MetricsFeatureId::HttpProxyType ||
                       id == MetricsFeatureId::CertificateSource || id == MetricsFeatureId::TlsCipherPreference ||
                       id == MetricsFeatureId::MinimumTlsVersion;
            }

            ////////// AWSIoTMetrics //////////

            AWSIoTMetrics::AWSIoTMetrics() : m_libraryName(Mqtt::IoTSDKMetricsEncoder::DEFAULT_METRICS_LIBRARY_NAME) {}

            AWSIoTMetrics::AWSIoTMetrics(const AWSIoTMetrics &other)
                : m_libraryName(other.m_libraryName), m_metadata(other.m_metadata)
            {
            }

            AWSIoTMetrics &AWSIoTMetrics::operator=(const AWSIoTMetrics &other)
            {
                if (this != &other)
                {
                    m_libraryName = other.m_libraryName;
                    m_metadata = other.m_metadata;
                    m_rawMetadataEntries.clear();
                }
                return *this;
            }

            AWSIoTMetrics::AWSIoTMetrics(AWSIoTMetrics &&other) noexcept
                : m_libraryName(std::move(other.m_libraryName)), m_metadata(std::move(other.m_metadata))
            {
                // m_rawMetadataEntries intentionally not moved — byte cursors would dangle.
                other.m_rawMetadataEntries.clear();
            }

            AWSIoTMetrics &AWSIoTMetrics::operator=(AWSIoTMetrics &&other) noexcept
            {
                if (this != &other)
                {
                    m_libraryName = std::move(other.m_libraryName);
                    m_metadata = std::move(other.m_metadata);
                    m_rawMetadataEntries.clear();
                    other.m_rawMetadataEntries.clear();
                }
                return *this;
            }

            void AWSIoTMetrics::SetLibraryName(Aws::Crt::String name)
            {
                m_libraryName = std::move(name);
                resetRawData();
            }

            const Aws::Crt::String &AWSIoTMetrics::GetLibraryName() const
            {
                return m_libraryName;
            }

            void AWSIoTMetrics::SetMetadataEntry(const Crt::String &key, const Crt::String &value)
            {
                m_metadata[key] = value;
                resetRawData();
            }

            void AWSIoTMetrics::RemoveMetadataEntry(const Crt::String &key)
            {
                m_metadata.erase(key);
                resetRawData();
            }

            const Crt::Map<Crt::String, Crt::String> &AWSIoTMetrics::GetMetadata() const
            {
                return m_metadata;
            }

            void AWSIoTMetrics::resetRawData() noexcept
            {
                m_rawMetadataEntries.clear();
            }

            void AWSIoTMetrics::initializeRawOptions(struct aws_mqtt_iot_metrics &raw_options) noexcept
            {
                raw_options.library_name = ByteCursorFromString(m_libraryName);

                // Rebuild the raw entry array from the current m_metadata contents.
                // Byte cursors point directly into the strings stored in m_metadata.
                resetRawData();
                m_rawMetadataEntries.reserve(m_metadata.size());
                for (const auto &entry : m_metadata)
                {
                    aws_mqtt_metadata_entry rawEntry;
                    rawEntry.key = ByteCursorFromString(entry.first);
                    rawEntry.value = ByteCursorFromString(entry.second);
                    m_rawMetadataEntries.push_back(rawEntry);
                }

                raw_options.metadata_count = m_rawMetadataEntries.size();
                raw_options.metadata_entries = m_rawMetadataEntries.empty() ? nullptr : m_rawMetadataEntries.data();
            }

            /*! \cond DOXYGEN_PRIVATE */
            ////////// IoTSDKMetricsEncoder //////////
            AWSIoTMetrics IoTSDKMetricsEncoder::createMetricsForMqtt5(const Mqtt5::Mqtt5ClientOptions &options)
            {
                AWSIoTMetrics outMetrics;
                Crt::String crtFeatureList = getEncodedFeatureListForMqtt5(options);

                // Get user-provided metrics from the options
                const AWSIoTMetrics *userMetrics =
                    options.m_sdkMetrics.has_value() ? &options.m_sdkMetrics.value() : nullptr;

                createMetricsFromFeatureList(crtFeatureList, userMetrics, outMetrics);
                return outMetrics;
            }

            AWSIoTMetrics IoTSDKMetricsEncoder::createMetricsForMqtt311(const MqttConnectionCore &connectionCore)
            {
                AWSIoTMetrics outMetrics;
                Crt::String crtFeatureList = getEncodedFeatureListForMqtt311(connectionCore);
                const AWSIoTMetrics *userMetrics =
                    connectionCore.m_sdkMetrics.has_value() ? &connectionCore.m_sdkMetrics.value() : nullptr;
                createMetricsFromFeatureList(crtFeatureList, userMetrics, outMetrics);
                return outMetrics;
            }

            Crt::String IoTSDKMetricsEncoder::getEncodedFeatureListForMqtt311(const MqttConnectionCore &connectionCore)
            {
                Crt::String features;

                // F: protocol_version — MQTT 3.1.1 is always used
                appendFeature(features, MetricsFeatureId::ProtocolVersion, MetricsProtocolVersionValue::Mqtt311);

                // G: socket_implementation — detected at compile time
                appendFeature(features, MetricsFeatureId::SocketImplementation, detectSocketImplementation());

                // H: http_proxy_type — set if a proxy is configured
                if (connectionCore.m_proxyOptions.has_value())
                {
                    bool proxyUsesTls = connectionCore.m_proxyOptions->TlsOptions.has_value() &&
                                        connectionCore.m_proxyOptions->ProxyConnectionType ==
                                            Crt::Http::AwsHttpProxyConnectionType::Tunneling;
                    appendFeature(
                        features,
                        MetricsFeatureId::HttpProxyType,
                        proxyUsesTls ? MetricsHttpProxyTypeValue::Https : MetricsHttpProxyTypeValue::Http);
                }
                if (connectionCore.m_useTls)
                {
                    Io::TlsConnectionInfo tlsInfo = connectionCore.m_tlsOptions.GetTlsConnectionInfo();

                    // I: certificate_source — automatically derived from TlsConnectionOptions
                    appendFeature(
                        features,
                        MetricsFeatureId::CertificateSource,
                        metricsValueForCertificateSource(tlsInfo.certificateSource));

                    // J: tls_cipher_preference
                    appendFeature(
                        features,
                        MetricsFeatureId::TlsCipherPreference,
                        metricsValueForTlsCipherPreference(tlsInfo.cipherPref));

                    // K: minimum_tls_version
                    appendFeature(
                        features,
                        MetricsFeatureId::MinimumTlsVersion,
                        metricsValueForMinimumTlsVersion(tlsInfo.tlsVersion));
                }

                return features;
            }

            Crt::String IoTSDKMetricsEncoder::getEncodedFeatureListForMqtt5(const Mqtt5::Mqtt5ClientOptions &options)
            {
                Crt::String features;

                // A: retry_jitter_mode
                appendFeature(
                    features,
                    MetricsFeatureId::RetryJitterMode,
                    metricsValueForRetryJitterMode(options.m_reconnectionOptions.m_reconnectMode));

                // B: session_behavior
                appendFeature(
                    features,
                    MetricsFeatureId::SessionBehavior,
                    metricsValueForSessionBehavior(options.m_sessionBehavior));

                // C: offline_queue_behavior
                appendFeature(
                    features,
                    MetricsFeatureId::OfflineQueueBehavior,
                    metricsValueForOfflineQueueBehavior(options.m_offlineQueueBehavior));

                // D: outbound_topic_alias_behavior
                appendFeature(
                    features,
                    MetricsFeatureId::OutboundTopicAliasBehavior,
                    metricsValueForOutboundTopicAliasBehavior(
                        static_cast<Mqtt5::OutboundTopicAliasBehaviorType>(
                            options.m_topicAliasingOptions.outbound_topic_alias_behavior)));

                // E: inbound_topic_alias_behavior
                appendFeature(
                    features,
                    MetricsFeatureId::InboundTopicAliasBehavior,
                    metricsValueForInboundTopicAliasBehavior(
                        static_cast<Mqtt5::InboundTopicAliasBehaviorType>(
                            options.m_topicAliasingOptions.inbound_topic_alias_behavior)));

                // F: protocol_version — MQTT5 is always used for Mqtt5Client
                appendFeature(features, MetricsFeatureId::ProtocolVersion, MetricsProtocolVersionValue::Mqtt5);

                // G: socket_implementation — detected at compile time
                appendFeature(features, MetricsFeatureId::SocketImplementation, detectSocketImplementation());

                // H: http_proxy_type — set if a proxy is configured
                if (options.m_proxyOptions.has_value())
                {
                    bool proxyUsesTls =
                        options.m_proxyOptions->TlsOptions.has_value() &&
                        options.m_proxyOptions->ProxyConnectionType == Crt::Http::AwsHttpProxyConnectionType::Tunneling;
                    appendFeature(
                        features,
                        MetricsFeatureId::HttpProxyType,
                        proxyUsesTls ? MetricsHttpProxyTypeValue::Https : MetricsHttpProxyTypeValue::Http);
                }

                if (options.m_tlsConnectionOptions.has_value())
                {
                    Io::TlsConnectionInfo tlsInfo = options.m_tlsConnectionOptions->GetTlsConnectionInfo();

                    // I: certificate_source — automatically derived from TlsConnectionOptions
                    appendFeature(
                        features,
                        MetricsFeatureId::CertificateSource,
                        metricsValueForCertificateSource(tlsInfo.certificateSource));

                    // J: tls_cipher_preference
                    appendFeature(
                        features,
                        MetricsFeatureId::TlsCipherPreference,
                        metricsValueForTlsCipherPreference(tlsInfo.cipherPref));

                    // K: minimum_tls_version
                    appendFeature(
                        features,
                        MetricsFeatureId::MinimumTlsVersion,
                        metricsValueForMinimumTlsVersion(tlsInfo.tlsVersion));
                }

                return features;
            }

            void IoTSDKMetricsEncoder::createMetricsFromFeatureList(
                const Crt::String &crtFeatureList,
                const AWSIoTMetrics *userMetrics,
                AWSIoTMetrics &outMetrics)
            {
                // Reset the output to defaults
                outMetrics.m_libraryName = IoTSDKMetricsEncoder::DEFAULT_METRICS_LIBRARY_NAME;
                outMetrics.m_metadata.clear();
                outMetrics.m_rawMetadataEntries.clear();

                // Determine the library name: use user-provided or default
                if (userMetrics != nullptr && !userMetrics->m_libraryName.empty())
                {
                    outMetrics.m_libraryName = userMetrics->m_libraryName;
                }

                // CRTVersion: not modifiable by user, automatically set
                outMetrics.m_metadata["CRTVersion"] = AWS_CRT_CPP_VERSION;

                Crt::String userFeatureString;

                if (userMetrics != nullptr)
                {
                    // Check if user provided a matching metrics version for feature merging
                    Crt::String userMetricsVersion;
                    Crt::String userFeature;

                    auto versionIt = userMetrics->m_metadata.find("IoTSDKMetricsVersion");
                    if (versionIt != userMetrics->m_metadata.end())
                    {
                        userMetricsVersion = versionIt->second;
                    }
                    auto featureIt = userMetrics->m_metadata.find("IoTSDKFeature");
                    if (featureIt != userMetrics->m_metadata.end())
                    {
                        userFeature = featureIt->second;
                    }

                    // Only merge user features if the metrics version matches
                    if (!userMetricsVersion.empty() &&
                        std::atoi(userMetricsVersion.c_str()) == IoTSDKMetricsFeatureVersion)
                    {
                        userFeatureString = userFeature;
                    }

                    // Preserve other user metadata (excluding reserved keys)
                    for (const auto &entry : userMetrics->m_metadata)
                    {
                        if (entry.first != "IoTSDKFeature" && entry.first != "IoTSDKMetricsVersion" &&
                            entry.first != "CRTVersion")
                        {
                            outMetrics.m_metadata[entry.first] = entry.second;
                        }
                    }
                }

                // Merge CRT and user features (both inputs are validated at this point)
                Crt::String mergedFeatures = mergeFeatureLists(crtFeatureList, userFeatureString);
                outMetrics.m_metadata["IoTSDKFeature"] = mergedFeatures;

                // Always add the current metrics version
                Crt::StringStream versionSS;
                versionSS << IoTSDKMetricsFeatureVersion;
                outMetrics.m_metadata["IoTSDKMetricsVersion"] = versionSS.str();
            }

            Crt::String IoTSDKMetricsEncoder::mergeFeatureLists(
                const Crt::String &crtFeatures,
                const Crt::String &userFeatures)
            {
                // If no user features to merge, return CRT features directly
                if (userFeatures.empty())
                {
                    return crtFeatures;
                }

                // Parse features into a map: featureId -> valueCode
                // User features are parsed second so they take precedence over CRT features.
                Crt::Map<Crt::String, Crt::String> featureMap;

                auto parseFeatureList = [&featureMap](const Crt::String &features)
                {
                    size_t pos = 0;
                    while (pos < features.size())
                    {
                        size_t commaPos = features.find(',', pos);
                        Crt::String token =
                            features.substr(pos, commaPos == Crt::String::npos ? Crt::String::npos : commaPos - pos);
                        size_t slashPos = token.find('/');
                        // A valid feature token must have the format "X/Y" where:
                        // - The feature ID is exactly one character (slashPos == 1)
                        // - The feature ID is a recognized MetricsFeatureId
                        // - The value (after slash) is non-empty
                        if (slashPos == 1 && slashPos + 1 < token.size() && isValidFeatureId(token[0]))
                        {
                            featureMap[token.substr(0, slashPos)] = token.substr(slashPos + 1);
                        }
                        if (commaPos == Crt::String::npos)
                        {
                            break;
                        }
                        pos = commaPos + 1;
                    }
                };

                // Parse CRT features first, then user features (user takes precedence)
                parseFeatureList(crtFeatures);
                parseFeatureList(userFeatures);

                // Build merged string
                Crt::String result;
                for (const auto &entry : featureMap)
                {
                    if (!result.empty())
                    {
                        result += ',';
                    }
                    result += entry.first;
                    result += '/';
                    result += entry.second;
                }
                return result;
            }

            ////////// Extension mappings from existing enums to metrics values //////////

            char IoTSDKMetricsEncoder::metricsValueForRetryJitterMode(Mqtt5::ExponentialBackoffJitterMode mode)
            {
                switch (mode)
                {
                    case AWS_EXPONENTIAL_BACKOFF_JITTER_NONE:
                        return 'A';
                    case AWS_EXPONENTIAL_BACKOFF_JITTER_FULL:
                        return 'B';
                    case AWS_EXPONENTIAL_BACKOFF_JITTER_DECORRELATED:
                        return 'C';
                    default:
                        return '\0';
                }
            }

            char IoTSDKMetricsEncoder::metricsValueForSessionBehavior(Mqtt5::ClientSessionBehaviorType behavior)
            {
                switch (behavior)
                {
                    case AWS_MQTT5_CSBT_CLEAN:
                        return 'A';
                    case AWS_MQTT5_CSBT_REJOIN_POST_SUCCESS:
                        return 'B';
                    case AWS_MQTT5_CSBT_REJOIN_ALWAYS:
                        return 'C';
                    default:
                        return '\0';
                }
            }

            char IoTSDKMetricsEncoder::metricsValueForOfflineQueueBehavior(
                Mqtt5::ClientOperationQueueBehaviorType behavior)
            {
                switch (behavior)
                {
                    case AWS_MQTT5_COQBT_FAIL_NON_QOS1_PUBLISH_ON_DISCONNECT:
                        return 'A';
                    case AWS_MQTT5_COQBT_FAIL_QOS0_PUBLISH_ON_DISCONNECT:
                        return 'B';
                    case AWS_MQTT5_COQBT_FAIL_ALL_ON_DISCONNECT:
                        return 'C';
                    default:
                        return '\0';
                }
            }

            char IoTSDKMetricsEncoder::metricsValueForOutboundTopicAliasBehavior(
                Mqtt5::OutboundTopicAliasBehaviorType behavior)
            {
                switch (behavior)
                {
                    case Mqtt5::OutboundTopicAliasBehaviorType::Manual:
                        return 'A';
                    case Mqtt5::OutboundTopicAliasBehaviorType::LRU:
                        return 'B';
                    case Mqtt5::OutboundTopicAliasBehaviorType::Disabled:
                        return 'C';
                    default:
                        return '\0';
                }
            }

            char IoTSDKMetricsEncoder::metricsValueForInboundTopicAliasBehavior(
                Mqtt5::InboundTopicAliasBehaviorType behavior)
            {
                switch (behavior)
                {
                    case Mqtt5::InboundTopicAliasBehaviorType::Enabled:
                        return 'A';
                    case Mqtt5::InboundTopicAliasBehaviorType::Disabled:
                        return 'B';
                    default:
                        return '\0';
                }
            }

            char IoTSDKMetricsEncoder::metricsValueForTlsCipherPreference(aws_tls_cipher_pref pref)
            {
                switch (pref)
                {
                    case AWS_IO_TLS_CIPHER_PREF_KMS_PQ_TLSv1_0_2019_06: // enum 1
                        return MetricsTlsCipherPreferenceValue::KmsPqTlsv10_2019_06;
                    case AWS_IO_TLS_CIPHER_PREF_KMS_PQ_SIKE_TLSv1_0_2019_11: // enum 2
                        return MetricsTlsCipherPreferenceValue::KmsPqSikeTlsv10_2019_11;
                    case AWS_IO_TLS_CIPHER_PREF_KMS_PQ_TLSv1_0_2020_02: // enum 3
                        return MetricsTlsCipherPreferenceValue::KmsPqTlsv10_2020_02;
                    case AWS_IO_TLS_CIPHER_PREF_KMS_PQ_SIKE_TLSv1_0_2020_02: // enum 4
                        return MetricsTlsCipherPreferenceValue::KmsPqSikeTlsv10_2020_02;
                    case AWS_IO_TLS_CIPHER_PREF_KMS_PQ_TLSv1_0_2020_07: // enum 5
                        return MetricsTlsCipherPreferenceValue::KmsPqTlsv10_2020_07;
                    case AWS_IO_TLS_CIPHER_PREF_PQ_TLSv1_0_2021_05: // enum 6
                        return MetricsTlsCipherPreferenceValue::PqTlsv10_2021_05;
                    case AWS_IO_TLS_CIPHER_PREF_PQ_TLSV1_2_2024_10: // enum 7
                        return MetricsTlsCipherPreferenceValue::PqTlsv12_2024_10;
                    case AWS_IO_TLS_CIPHER_PREF_PQ_DEFAULT: // enum 8
                        return MetricsTlsCipherPreferenceValue::PqDefault;
                    case AWS_IO_TLS_CIPHER_PREF_TLSV1_2_2025_07: // enum 9
                        return MetricsTlsCipherPreferenceValue::Tlsv12_2025_07;
                    case AWS_IO_TLS_CIPHER_PREF_TLSV1_0_2023_06: // enum 10
                        return MetricsTlsCipherPreferenceValue::Tlsv10_2023_06;
                    case AWS_IO_TLS_CIPHER_PREF_NON_PQ_DEFAULT: // enum 11
                        return MetricsTlsCipherPreferenceValue::NonPqDefault;
                    default:
                        return '\0';
                }
            }

            char IoTSDKMetricsEncoder::metricsValueForCertificateSource(Io::CertificateSource source)
            {
                switch (source)
                {
                    case Io::CertificateSource::CertificateFiles:
                        return MetricsCertificateSourceValue::CertificateFiles;
                    case Io::CertificateSource::Pkcs11:
                        return MetricsCertificateSourceValue::Pkcs11;
                    case Io::CertificateSource::WindowsCertStore:
                        return MetricsCertificateSourceValue::WindowsCertStore;
                    case Io::CertificateSource::Pkcs12File:
                        return MetricsCertificateSourceValue::Pkcs12File;
                    default:
                        return '\0';
                }
            }

            char IoTSDKMetricsEncoder::metricsValueForMinimumTlsVersion(aws_tls_versions version)
            {
                switch (version)
                {
                    case AWS_IO_SSLv3:
                        return MetricsMinimumTlsVersionValue::SSLv3;
                    case AWS_IO_TLSv1:
                        return MetricsMinimumTlsVersionValue::TLSv1;
                    case AWS_IO_TLSv1_1:
                        return MetricsMinimumTlsVersionValue::TLSv1_1;
                    case AWS_IO_TLSv1_2:
                        return MetricsMinimumTlsVersionValue::TLSv1_2;
                    case AWS_IO_TLSv1_3:
                        return MetricsMinimumTlsVersionValue::TLSv1_3;
                    default:
                        return '\0';
                }
            }

            char IoTSDKMetricsEncoder::detectSocketImplementation()
            {
#if defined(_WIN32)
                return MetricsSocketImplementationValue::Winsock;
#elif defined(AWS_USE_SECITEM)
                return MetricsSocketImplementationValue::AppleNetworkFramework;
#else
                return MetricsSocketImplementationValue::Posix;
#endif
            }

            void IoTSDKMetricsEncoder::appendFeature(Crt::String &featureList, char featureId, char value)
            {
                if (value == '\0')
                {
                    return;
                }
                if (!featureList.empty())
                {
                    featureList += ',';
                }
                featureList += featureId;
                featureList += '/';
                featureList += value;
            }

            /*! \endcond */
        } // namespace Mqtt
    } // namespace Crt
} // namespace Aws
