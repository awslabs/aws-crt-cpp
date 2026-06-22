/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/mqtt/IoTSDKMetrics.h>
#include <aws/crt/mqtt/private/IoTSDKMetricsPrivate.h>
#include <aws/crt/mqtt/private/MqttConnectionCore.h>

#include <aws/crt/Config.h>
#include <aws/crt/io/private/CertificateSource.h>

#include <cstdlib>

namespace Aws
{
    namespace Crt
    {
        namespace Mqtt
        {
            ////////// IoTDeviceSDKMetrics //////////

            void IoTDeviceSDKMetrics::AddMetadata(const Crt::String &key, const Crt::String &value) noexcept
            {
                Metadata[key] = value;
            }

            void IoTDeviceSDKMetrics::initializeRawOptions(struct aws_mqtt_iot_metrics &raw_options) noexcept
            {
                raw_options.library_name = ByteCursorFromString(LibraryName);

                // Rebuild the raw entry array from the current Metadata contents.
                // Byte cursors point directly into the strings stored in Metadata.
                m_rawMetadataEntries.clear();
                m_rawMetadataEntries.reserve(Metadata.size());
                for (const auto &entry : Metadata)
                {
                    aws_mqtt_metadata_entry rawEntry;
                    rawEntry.key = ByteCursorFromString(entry.first);
                    rawEntry.value = ByteCursorFromString(entry.second);
                    m_rawMetadataEntries.push_back(rawEntry);
                }

                raw_options.metadata_count = m_rawMetadataEntries.size();
                raw_options.metadata_entries = m_rawMetadataEntries.empty() ? nullptr : m_rawMetadataEntries.data();
            }

            ////////// IoTSDKMetricsEncoder //////////

            IoTDeviceSDKMetrics IoTSDKMetricsEncoder::createMetricsForMqtt5(const Mqtt5::Mqtt5ClientOptions &options)
            {
                Crt::String crtFeatureList = getEncodedFeatureListForMqtt5(options);

                // Get user-provided metrics from the options
                const IoTDeviceSDKMetrics *userMetrics =
                    options.m_sdkMetrics.has_value() ? &options.m_sdkMetrics.value() : nullptr;

                return createMetricsFromFeatureList(crtFeatureList, userMetrics);
            }

            IoTDeviceSDKMetrics IoTSDKMetricsEncoder::createMetricsForMqtt311(const MqttConnectionCore &connectionCore)
            {
                Crt::String crtFeatureList = getEncodedFeatureListForMqtt311(connectionCore);
                const IoTDeviceSDKMetrics *userMetrics =
                    connectionCore.m_sdkMetrics.has_value() ? &connectionCore.m_sdkMetrics.value() : nullptr;
                return createMetricsFromFeatureList(crtFeatureList, userMetrics);
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
                    const Io::TlsConnectionOptions &tlsOptions = connectionCore.m_tlsOptions;

                    // I: certificate_source — automatically derived from TlsConnectionOptions
                    appendFeature(
                        features,
                        MetricsFeatureId::CertificateSource,
                        metricsValueForCertificateSource(
                            static_cast<Io::CertificateSource>(tlsOptions.m_metricsCertificateSource)));

                    // J: tls_cipher_preference
                    appendFeature(
                        features,
                        MetricsFeatureId::TlsCipherPreference,
                        metricsValueForTlsCipherPreference(tlsOptions.m_cipherPref));

                    // K: minimum_tls_version
                    appendFeature(
                        features,
                        MetricsFeatureId::MinimumTlsVersion,
                        metricsValueForMinimumTlsVersion(tlsOptions.m_tlsVersion));
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
                    metricsValueForOutboundTopicAliasBehavior(static_cast<Mqtt5::OutboundTopicAliasBehaviorType>(
                        options.m_topicAliasingOptions.outbound_topic_alias_behavior)));

                // E: inbound_topic_alias_behavior
                appendFeature(
                    features,
                    MetricsFeatureId::InboundTopicAliasBehavior,
                    metricsValueForInboundTopicAliasBehavior(static_cast<Mqtt5::InboundTopicAliasBehaviorType>(
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
                    // I: certificate_source — automatically derived from TlsConnectionOptions
                    appendFeature(
                        features,
                        MetricsFeatureId::CertificateSource,
                        metricsValueForCertificateSource(static_cast<Io::CertificateSource>(
                            options.m_tlsConnectionOptions->m_metricsCertificateSource)));

                    // J: tls_cipher_preference
                    appendFeature(
                        features,
                        MetricsFeatureId::TlsCipherPreference,
                        metricsValueForTlsCipherPreference(options.m_tlsConnectionOptions->m_cipherPref));

                    // K: minimum_tls_version
                    appendFeature(
                        features,
                        MetricsFeatureId::MinimumTlsVersion,
                        metricsValueForMinimumTlsVersion(options.m_tlsConnectionOptions->m_tlsVersion));
                }

                return features;
            }

            IoTDeviceSDKMetrics IoTSDKMetricsEncoder::createMetricsFromFeatureList(
                const Crt::String &crtFeatureList,
                const IoTDeviceSDKMetrics *userMetrics)
            {
                // Determine the library name: use user-provided or default
                IoTDeviceSDKMetrics resultMetrics;
                if (userMetrics != nullptr && userMetrics->LibraryName != "IoTDeviceSDK/CPP")
                {
                    resultMetrics.LibraryName = userMetrics->LibraryName;
                }

                // CRTVersion: not modifiable by user, automatically set
                resultMetrics.AddMetadata("CRTVersion", AWS_CRT_CPP_VERSION);

                Crt::String userFeatureString;

                if (userMetrics != nullptr)
                {
                    // Check if user provided a matching metrics version for feature merging
                    Crt::String userMetricsVersion;
                    Crt::String userFeature;

                    auto versionIt = userMetrics->Metadata.find("IoTSDKMetricsVersion");
                    if (versionIt != userMetrics->Metadata.end())
                    {
                        userMetricsVersion = versionIt->second;
                    }
                    auto featureIt = userMetrics->Metadata.find("IoTSDKFeature");
                    if (featureIt != userMetrics->Metadata.end())
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
                    for (const auto &entry : userMetrics->Metadata)
                    {
                        if (entry.first != "IoTSDKFeature" && entry.first != "IoTSDKMetricsVersion" &&
                            entry.first != "CRTVersion")
                        {
                            resultMetrics.Metadata[entry.first] = entry.second;
                        }
                    }
                }

                // Merge CRT and user features
                Crt::String mergedFeatures = mergeFeatureLists(crtFeatureList, userFeatureString);
                resultMetrics.AddMetadata("IoTSDKFeature", mergedFeatures);

                // Always add the current metrics version
                Crt::StringStream versionSS;
                versionSS << IoTSDKMetricsFeatureVersion;
                resultMetrics.AddMetadata("IoTSDKMetricsVersion", versionSS.str());

                return resultMetrics;
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
                        if (slashPos != Crt::String::npos && slashPos > 0 && slashPos + 1 < token.size())
                        {
                            featureMap[token.substr(0, slashPos)] = token.substr(slashPos + 1);
                        }
                        if (commaPos == Crt::String::npos)
                            break;
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
                        result += ',';
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
#elif defined(__APPLE__)
                return MetricsSocketImplementationValue::AppleNetworkFramework;
#else
                return MetricsSocketImplementationValue::Posix;
#endif
            }

            void IoTSDKMetricsEncoder::appendFeature(Crt::String &featureList, char featureId, char value)
            {
                if (value == '\0')
                    return;
                if (!featureList.empty())
                    featureList += ',';
                featureList += featureId;
                featureList += '/';
                featureList += value;
            }

        } // namespace Mqtt
    } // namespace Crt
} // namespace Aws
