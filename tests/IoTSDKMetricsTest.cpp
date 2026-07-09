/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/Config.h>
#include <aws/crt/mqtt/IoTSDKMetrics.h>
#include <aws/crt/mqtt/Mqtt5Client.h>
#include <aws/crt/mqtt/MqttClient.h>
#include <aws/testing/aws_test_harness.h>

#include <aws/mqtt/v5/mqtt5_client.h>

using namespace Aws::Crt;
using namespace Aws::Crt::Mqtt;
using namespace Aws::Crt::Mqtt5;

//////////////////////////////////////////////////////////
// Test Helpers
//////////////////////////////////////////////////////////

/**
 * Helper to extract a metadata value from the raw aws_mqtt_iot_metrics struct.
 * Returns empty string if key not found.
 */
static Aws::Crt::String s_getMetadataValue(const struct aws_mqtt_iot_metrics *metrics, const char *key)
{
    if (metrics == nullptr || metrics->metadata_entries == nullptr)
        return "";
    for (size_t i = 0; i < metrics->metadata_count; ++i)
    {
        Aws::Crt::String k(
            reinterpret_cast<const char *>(metrics->metadata_entries[i].key.ptr), metrics->metadata_entries[i].key.len);
        if (k == key)
        {
            return Aws::Crt::String(
                reinterpret_cast<const char *>(metrics->metadata_entries[i].value.ptr),
                metrics->metadata_entries[i].value.len);
        }
    }
    return "";
}

/**
 * Helper to get the library name from raw metrics.
 */
static Aws::Crt::String s_getLibraryName(const struct aws_mqtt_iot_metrics *metrics)
{
    if (metrics == nullptr || metrics->library_name.ptr == nullptr)
        return "";
    return Aws::Crt::String(reinterpret_cast<const char *>(metrics->library_name.ptr), metrics->library_name.len);
}

/**
 * Helper to call initializeRawOptions on Mqtt5ClientOptions and return the metrics pointer.
 * The raw_options struct must outlive any use of the returned pointer.
 */
static const struct aws_mqtt_iot_metrics *s_getMetricsFromOptions(
    const Mqtt5ClientOptions &options,
    aws_mqtt5_client_options &raw_options)
{
    options.initializeRawOptions(raw_options);
    return raw_options.metrics;
}

static bool s_contains(const Aws::Crt::String &list, const Aws::Crt::String &token)
{
    size_t pos = 0;
    while (pos < list.size())
    {
        size_t c = list.find(',', pos);
        Aws::Crt::String cur = list.substr(pos, c == Aws::Crt::String::npos ? Aws::Crt::String::npos : c - pos);
        if (cur == token)
            return true;
        if (c == Aws::Crt::String::npos)
            break;
        pos = c + 1;
    }
    return false;
}

static bool s_containsPrefix(const Aws::Crt::String &list, const Aws::Crt::String &prefix)
{
    size_t pos = 0;
    while (pos < list.size())
    {
        size_t c = list.find(',', pos);
        Aws::Crt::String cur = list.substr(pos, c == Aws::Crt::String::npos ? Aws::Crt::String::npos : c - pos);
        if (cur.find(prefix) == 0)
            return true;
        if (c == Aws::Crt::String::npos)
            break;
        pos = c + 1;
    }
    return false;
}

static size_t s_partCount(const Aws::Crt::String &list)
{
    if (list.empty())
        return 0;
    size_t count = 1;
    for (char ch : list)
        if (ch == ',')
            ++count;
    return count;
}

/**
 * Returns the expected socket implementation value for the current platform.
 */
static char s_socketVal()
{
#if defined(_WIN32)
    return 'B'; // Winsock
#elif defined(__APPLE__)
    return 'C'; // Apple Network Framework
#else
    return 'A'; // Posix
#endif
}

//////////////////////////////////////////////////////////
// Minimal Options Encoding (MQTT5)
//////////////////////////////////////////////////////////

static int s_TestIoTSDKMetricsMqtt5Minimal(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);

    aws_mqtt5_client_options raw_options;
    const auto *metrics = s_getMetricsFromOptions(options, raw_options);
    ASSERT_NOT_NULL(metrics);

    Aws::Crt::String features = s_getMetadataValue(metrics, "IoTSDKFeature");
    ASSERT_TRUE(s_contains(features, "F/5"));
    ASSERT_TRUE(s_contains(features, Aws::Crt::String("G/") + s_socketVal()));
    ASSERT_INT_EQUALS(2, (int)s_partCount(features));
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsMqtt5Minimal, s_TestIoTSDKMetricsMqtt5Minimal)

//////////////////////////////////////////////////////////
// Default Values Omitted
//////////////////////////////////////////////////////////

static int s_TestIoTSDKMetricsDefaultValuesOmitted(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);
    ReconnectOptions ro = {AWS_EXPONENTIAL_BACKOFF_JITTER_DEFAULT, 1000, 1000, 1000};
    options.WithReconnectOptions(ro);
    options.WithSessionBehavior(AWS_MQTT5_CSBT_DEFAULT);
    options.WithOfflineQueueBehavior(AWS_MQTT5_COQBT_DEFAULT);

    aws_mqtt5_client_options raw_options;
    const auto *metrics = s_getMetricsFromOptions(options, raw_options);
    ASSERT_NOT_NULL(metrics);

    Aws::Crt::String features = s_getMetadataValue(metrics, "IoTSDKFeature");
    ASSERT_FALSE(s_containsPrefix(features, "A/"));
    ASSERT_FALSE(s_containsPrefix(features, "B/"));
    ASSERT_FALSE(s_containsPrefix(features, "C/"));
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsDefaultValuesOmitted, s_TestIoTSDKMetricsDefaultValuesOmitted)

//////////////////////////////////////////////////////////
// Multiple Non-Default Features
//////////////////////////////////////////////////////////

static int s_TestIoTSDKMetricsAllFeaturesSet(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);
    ReconnectOptions ro = {AWS_EXPONENTIAL_BACKOFF_JITTER_FULL, 1000, 1000, 1000};
    options.WithReconnectOptions(ro);
    options.WithSessionBehavior(AWS_MQTT5_CSBT_CLEAN);
    options.WithOfflineQueueBehavior(AWS_MQTT5_COQBT_FAIL_ALL_ON_DISCONNECT);
    TopicAliasingOptions ta;
    ta.m_outboundBehavior = OutboundTopicAliasBehaviorType::LRU;
    ta.m_inboundBehavior = InboundTopicAliasBehaviorType::Enabled;
    options.WithTopicAliasingOptions(ta);
    Http::HttpClientConnectionProxyOptions proxy;
    proxy.HostName = "proxy.example.com";
    proxy.Port = 8080;
    proxy.ProxyConnectionType = Http::AwsHttpProxyConnectionType::Tunneling;
    options.WithHttpProxyOptions(proxy);

    // TLS context creation requires a real crypto implementation. With BYO_CRYPTO, TlsContext cannot be created without
    // registering custom callbacks, so we skip TLS-related setup and assertions in that configuration.
#if !BYO_CRYPTO
    Io::TlsContextOptions tlsOpts = Io::TlsContextOptions::InitDefaultClient(allocator);
    tlsOpts.SetMinimumTlsVersion(AWS_IO_TLSv1_2);
    Io::TlsContext tlsCtx(tlsOpts, Io::TlsMode::CLIENT, allocator);
    options.WithTlsConnectionOptions(tlsCtx.NewConnectionOptions());
#endif

    aws_mqtt5_client_options raw_options;
    const auto *metrics = s_getMetricsFromOptions(options, raw_options);
    ASSERT_NOT_NULL(metrics);

    Aws::Crt::String features = s_getMetadataValue(metrics, "IoTSDKFeature");
    AWS_LOGF_DEBUG(AWS_LS_MQTT5_GENERAL, "IoTSDKMetricsAllFeaturesSet features: %s", features.c_str());
    ASSERT_TRUE(s_contains(features, "A/B"));
    ASSERT_TRUE(s_contains(features, "B/A"));
    ASSERT_TRUE(s_contains(features, "C/C"));
    ASSERT_TRUE(s_contains(features, "D/B"));
    ASSERT_TRUE(s_contains(features, "E/A"));
    ASSERT_TRUE(s_contains(features, "F/5"));
    ASSERT_TRUE(s_contains(features, "H/A"));
#if !BYO_CRYPTO
    // K/D (minimum TLS version = TLSv1.2) is only verifiable when a TLS context can be created
    ASSERT_TRUE(s_contains(features, "K/D"));
#endif
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsAllFeaturesSet, s_TestIoTSDKMetricsAllFeaturesSet)

//////////////////////////////////////////////////////////
// Merge Feature Lists (tested indirectly through MQTT5)
//////////////////////////////////////////////////////////

static int s_TestIoTSDKMetricsMergeMultipleOverrides(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    // Set up MQTT5 options that produce known CRT features:
    // A/B (jitter FULL), F/5 (protocol), G/{socket}
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);
    ReconnectOptions ro = {AWS_EXPONENTIAL_BACKOFF_JITTER_FULL, 1000, 1000, 1000};
    options.WithReconnectOptions(ro);

    // TLS context creation requires a real crypto implementation. With BYO_CRYPTO, TlsContext cannot be created without
    // registering custom callbacks, so we skip TLS-related setup and assertions in that configuration.
#if !BYO_CRYPTO
    Io::TlsContextOptions tlsOpts = Io::TlsContextOptions::InitDefaultClient(allocator);
    tlsOpts.SetMinimumTlsVersion(AWS_IO_TLSv1_2);
    Io::TlsContext tlsCtx(tlsOpts, Io::TlsMode::CLIENT, allocator);
    options.WithTlsConnectionOptions(tlsCtx.NewConnectionOptions());
#endif

    // User features override A and K, and override F
    IoTDeviceSDKMetrics customMetrics;
    customMetrics.metadata["IoTSDKMetricsVersion"] = "1";
    customMetrics.metadata["IoTSDKFeature"] = "A/C,F/3,K/E";
    options.WithSdkMetrics(std::move(customMetrics));

    aws_mqtt5_client_options raw_options;
    const auto *metrics = s_getMetricsFromOptions(options, raw_options);
    ASSERT_NOT_NULL(metrics);

    Aws::Crt::String features = s_getMetadataValue(metrics, "IoTSDKFeature");
    // User overrides: A/C replaces A/B, F/3 replaces F/5, K/E replaces K/D
    ASSERT_TRUE(s_contains(features, "A/C"));
    ASSERT_FALSE(s_contains(features, "A/B"));
    ASSERT_TRUE(s_contains(features, "F/3"));
    ASSERT_FALSE(s_contains(features, "F/5"));
    ASSERT_TRUE(s_contains(features, "K/E"));
    ASSERT_FALSE(s_contains(features, "K/D"));
    // G/{socket} should still be present (not overridden)
    ASSERT_TRUE(s_contains(features, Aws::Crt::String("G/") + s_socketVal()));
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsMergeMultipleOverrides, s_TestIoTSDKMetricsMergeMultipleOverrides)

static int s_TestIoTSDKMetricsMergeEmptyUser(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);

    // No user metrics set — CRT features should be preserved as-is
    aws_mqtt5_client_options raw_options;
    const auto *metrics = s_getMetricsFromOptions(options, raw_options);
    ASSERT_NOT_NULL(metrics);

    Aws::Crt::String features = s_getMetadataValue(metrics, "IoTSDKFeature");
    ASSERT_TRUE(s_contains(features, "F/5"));
    ASSERT_TRUE(s_contains(features, Aws::Crt::String("G/") + s_socketVal()));
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsMergeEmptyUser, s_TestIoTSDKMetricsMergeEmptyUser)

static int s_TestIoTSDKMetricsMergeEmptyCrt(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    // Disable metrics to get no CRT features, then provide user features
    // Actually, if metrics are disabled, raw_options.metrics will be nullptr.
    // Instead, test that user features are added to CRT features
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);

    IoTDeviceSDKMetrics customMetrics;
    customMetrics.metadata["IoTSDKMetricsVersion"] = "1";
    customMetrics.metadata["IoTSDKFeature"] = "I/B";
    options.WithSdkMetrics(std::move(customMetrics));

    aws_mqtt5_client_options raw_options;
    const auto *metrics = s_getMetricsFromOptions(options, raw_options);
    ASSERT_NOT_NULL(metrics);

    Aws::Crt::String features = s_getMetadataValue(metrics, "IoTSDKFeature");
    // User feature I/B should be present alongside CRT features
    ASSERT_TRUE(s_contains(features, "I/B"));
    ASSERT_TRUE(s_contains(features, "F/5"));
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsMergeEmptyCrt, s_TestIoTSDKMetricsMergeEmptyCrt)

//////////////////////////////////////////////////////////
// Create Metrics Tests (tested indirectly through MQTT5)
//////////////////////////////////////////////////////////

static int s_TestIoTSDKMetricsCreateNullUser(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);

    aws_mqtt5_client_options raw_options;
    const auto *metrics = s_getMetricsFromOptions(options, raw_options);
    ASSERT_NOT_NULL(metrics);

    // Default library name
    Aws::Crt::String libraryName = s_getLibraryName(metrics);
    ASSERT_TRUE(libraryName == "IoTDeviceSDK/CPP");

    // CRTVersion should be set
    Aws::Crt::String crtVersion = s_getMetadataValue(metrics, "CRTVersion");
    ASSERT_FALSE(crtVersion.empty());

    // IoTSDKFeature should contain protocol and socket
    Aws::Crt::String features = s_getMetadataValue(metrics, "IoTSDKFeature");
    ASSERT_TRUE(s_contains(features, "F/5"));
    ASSERT_TRUE(s_contains(features, Aws::Crt::String("G/") + s_socketVal()));

    // IoTSDKMetricsVersion should be "1"
    Aws::Crt::String metricsVersion = s_getMetadataValue(metrics, "IoTSDKMetricsVersion");
    ASSERT_TRUE(metricsVersion == "1");

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsCreateNullUser, s_TestIoTSDKMetricsCreateNullUser)

static int s_TestIoTSDKMetricsCreateUserFeatureAdded(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);

    IoTDeviceSDKMetrics user;
    user.metadata["IoTSDKMetricsVersion"] = "1";
    user.metadata["IoTSDKFeature"] = "I/A";
    options.WithSdkMetrics(std::move(user));

    aws_mqtt5_client_options raw_options;
    const auto *metrics = s_getMetricsFromOptions(options, raw_options);
    ASSERT_NOT_NULL(metrics);

    Aws::Crt::String features = s_getMetadataValue(metrics, "IoTSDKFeature");
    ASSERT_TRUE(s_contains(features, "I/A"));
    ASSERT_TRUE(s_contains(features, "F/5"));
    ASSERT_TRUE(s_contains(features, Aws::Crt::String("G/") + s_socketVal()));
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsCreateUserFeatureAdded, s_TestIoTSDKMetricsCreateUserFeatureAdded)

static int s_TestIoTSDKMetricsCreateUserOverridesCrt(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);

    IoTDeviceSDKMetrics user;
    user.metadata["IoTSDKMetricsVersion"] = "1";
    user.metadata["IoTSDKFeature"] = "F/3,I/B";
    options.WithSdkMetrics(std::move(user));

    aws_mqtt5_client_options raw_options;
    const auto *metrics = s_getMetricsFromOptions(options, raw_options);
    ASSERT_NOT_NULL(metrics);

    Aws::Crt::String features = s_getMetadataValue(metrics, "IoTSDKFeature");
    ASSERT_TRUE(s_contains(features, "F/3"));
    ASSERT_FALSE(s_contains(features, "F/5"));
    ASSERT_TRUE(s_contains(features, "I/B"));
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsCreateUserOverridesCrt, s_TestIoTSDKMetricsCreateUserOverridesCrt)

static int s_TestIoTSDKMetricsVersionMismatch(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);

    IoTDeviceSDKMetrics user;
    user.metadata["IoTSDKMetricsVersion"] = "99";
    user.metadata["IoTSDKFeature"] = "I/A";
    options.WithSdkMetrics(std::move(user));

    aws_mqtt5_client_options raw_options;
    const auto *metrics = s_getMetricsFromOptions(options, raw_options);
    ASSERT_NOT_NULL(metrics);

    Aws::Crt::String features = s_getMetadataValue(metrics, "IoTSDKFeature");
    // Version mismatch: user features should NOT be merged
    ASSERT_FALSE(s_contains(features, "I/A"));
    ASSERT_TRUE(s_contains(features, "F/5"));
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsVersionMismatch, s_TestIoTSDKMetricsVersionMismatch)

static int s_TestIoTSDKMetricsCRTVersionNotModifiable(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);

    IoTDeviceSDKMetrics user;
    user.metadata["CRTVersion"] = "fake_version";
    options.WithSdkMetrics(std::move(user));

    aws_mqtt5_client_options raw_options;
    const auto *metrics = s_getMetricsFromOptions(options, raw_options);
    ASSERT_NOT_NULL(metrics);

    Aws::Crt::String crtVersion = s_getMetadataValue(metrics, "CRTVersion");
    ASSERT_FALSE(crtVersion.empty());
    ASSERT_FALSE(crtVersion == "fake_version");
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsCRTVersionNotModifiable, s_TestIoTSDKMetricsCRTVersionNotModifiable)

static int s_TestIoTSDKMetricsPreservesUserMetadata(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);

    IoTDeviceSDKMetrics user;
    user.metadata["IoTSDKVersion"] = "2.0.0";
    user.metadata["CustomKey"] = "custom_value";
    options.WithSdkMetrics(std::move(user));

    aws_mqtt5_client_options raw_options;
    const auto *metrics = s_getMetricsFromOptions(options, raw_options);
    ASSERT_NOT_NULL(metrics);

    ASSERT_TRUE(s_getMetadataValue(metrics, "IoTSDKVersion") == "2.0.0");
    ASSERT_TRUE(s_getMetadataValue(metrics, "CustomKey") == "custom_value");
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsPreservesUserMetadata, s_TestIoTSDKMetricsPreservesUserMetadata)

static int s_TestIoTSDKMetricsCustomLibraryName(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);

    IoTDeviceSDKMetrics user;
    user.libraryName = "MyCustomSDK/1.0";
    options.WithSdkMetrics(std::move(user));

    aws_mqtt5_client_options raw_options;
    const auto *metrics = s_getMetricsFromOptions(options, raw_options);
    ASSERT_NOT_NULL(metrics);

    Aws::Crt::String libraryName = s_getLibraryName(metrics);
    ASSERT_TRUE(libraryName == "MyCustomSDK/1.0");
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsCustomLibraryName, s_TestIoTSDKMetricsCustomLibraryName)

//////////////////////////////////////////////////////////
// End-to-end: MQTT5 with SDK Metrics
//////////////////////////////////////////////////////////

static int s_TestIoTSDKMetricsMqtt5WithSdkMetrics(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);

    // Set custom metrics
    IoTDeviceSDKMetrics customMetrics;
    customMetrics.libraryName = "Mqtt5TestSDK/2.0";
    customMetrics.metadata["IoTSDKMetricsVersion"] = "1";
    customMetrics.metadata["IoTSDKFeature"] = "I/A";
    customMetrics.metadata["AppVersion"] = "3.0.0";
    options.WithSdkMetrics(std::move(customMetrics));

    aws_mqtt5_client_options raw_options;
    const auto *metrics = s_getMetricsFromOptions(options, raw_options);
    ASSERT_NOT_NULL(metrics);

    Aws::Crt::String libraryName = s_getLibraryName(metrics);
    ASSERT_TRUE(libraryName == "Mqtt5TestSDK/2.0");

    Aws::Crt::String features = s_getMetadataValue(metrics, "IoTSDKFeature");
    ASSERT_TRUE(s_contains(features, "I/A"));
    ASSERT_TRUE(s_contains(features, "F/5"));

    ASSERT_TRUE(s_getMetadataValue(metrics, "AppVersion") == "3.0.0");

    Aws::Crt::String crtVersion = s_getMetadataValue(metrics, "CRTVersion");
    ASSERT_TRUE(crtVersion == AWS_CRT_CPP_VERSION);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsMqtt5WithSdkMetrics, s_TestIoTSDKMetricsMqtt5WithSdkMetrics)

//////////////////////////////////////////////////////////
// MQTT3 Connection Creation Test
//////////////////////////////////////////////////////////
// We dont have a way to test Mqtt3 client metrics setup. Here we quickly set the connection and verify the
// connection is setup properly with the metrics API.
static int s_TestIoTSDKMetricsMqtt3Minimal(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Io::SocketOptions socketOptions;

    // Create custom metrics to pass to the MQTT3 connection
    IoTDeviceSDKMetrics customMetrics;
    customMetrics.libraryName = "Mqtt3TestSDK/1.0";
    customMetrics.metadata["IoTSDKMetricsVersion"] = "1";
    customMetrics.metadata["IoTSDKFeature"] = "I/B";
    customMetrics.metadata["AppVersion"] = "2.0.0";

    MqttClient mqttClient(allocator);
    ASSERT_TRUE(mqttClient);

    auto connection = mqttClient.NewConnection(
        "localhost", 8883, socketOptions, false /* no websocket */, true /* enableMetrics */, customMetrics);
    ASSERT_NOT_NULL(connection.get());
    ASSERT_TRUE(*connection);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsMqtt3Minimal, s_TestIoTSDKMetricsMqtt3Minimal)
