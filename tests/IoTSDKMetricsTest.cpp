/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/mqtt/IoTSDKMetrics.h>
#include <aws/crt/mqtt/Mqtt5Client.h>
#include <aws/crt/mqtt/private/IoTSDKMetricsPrivate.h>
#include <aws/crt/mqtt/private/MqttConnectionCore.h>
#include <aws/testing/aws_test_harness.h>

using namespace Aws::Crt;
using namespace Aws::Crt::Mqtt;
using namespace Aws::Crt::Mqtt5;

namespace Aws
{
    namespace Crt
    {
        namespace Mqtt
        {
            class IoTSDKMetricsTestHelper
            {
              public:
                static Aws::Crt::String GetEncodedFeatureListForMqtt5(const Mqtt5ClientOptions &options)
                {
                    return IoTSDKMetricsEncoder::getEncodedFeatureListForMqtt5(options);
                }
                static Aws::Crt::String GetEncodedFeatureListForMqtt311(const MqttConnectionCore &connection)
                {
                    return IoTSDKMetricsEncoder::getEncodedFeatureListForMqtt311(connection);
                }

                // Create a fake MqttConnectionCore for testing
                static std::shared_ptr<MqttConnectionCore> CreateTestConnectionCore(MqttConnectionOptions opts)
                {
                    auto *allocator = opts.allocator;
                    auto *toSeat =
                        reinterpret_cast<MqttConnectionCore *>(aws_mem_acquire(allocator, sizeof(MqttConnectionCore)));
                    toSeat = new (toSeat) MqttConnectionCore(nullptr, nullptr, nullptr, std::move(opts));
                    return std::shared_ptr<MqttConnectionCore>(
                        toSeat, [allocator](MqttConnectionCore *ptr) { Crt::Delete(ptr, allocator); });
                }
                static Aws::Crt::String MergeFeatureLists(
                    const Aws::Crt::String &crtFeatures,
                    const Aws::Crt::String &userFeatures)
                {
                    return IoTSDKMetricsEncoder::mergeFeatureLists(crtFeatures, userFeatures);
                }
                static IoTDeviceSDKMetrics CreateMetricsFromFeatureList(
                    const Aws::Crt::String &crtFeatureList,
                    const IoTDeviceSDKMetrics *userMetrics)
                {
                    return IoTSDKMetricsEncoder::createMetricsFromFeatureList(crtFeatureList, userMetrics);
                }
                static char DetectSocketImplementation() { return IoTSDKMetricsEncoder::detectSocketImplementation(); }
            };
        } // namespace Mqtt
    } // namespace Crt
} // namespace Aws

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

static char s_socketVal()
{
    return IoTSDKMetricsTestHelper::DetectSocketImplementation();
}

//////////////////////////////////////////////////////////
// Minimal Options Encoding
//////////////////////////////////////////////////////////

static int s_TestIoTSDKMetricsMqtt5Minimal(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);

    Aws::Crt::String result = IoTSDKMetricsTestHelper::GetEncodedFeatureListForMqtt5(options);
    ASSERT_TRUE(s_contains(result, "F/5"));
    ASSERT_TRUE(s_contains(result, Aws::Crt::String("G/") + s_socketVal()));
    ASSERT_INT_EQUALS(2, (int)s_partCount(result));
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsMqtt5Minimal, s_TestIoTSDKMetricsMqtt5Minimal)

static int s_TestIoTSDKMetricsMqtt3Minimal(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    MqttConnectionOptions opts;
    opts.allocator = allocator;
    opts.hostName = "localhost";
    opts.port = 8883;
    opts.useTls = false;
    auto core = IoTSDKMetricsTestHelper::CreateTestConnectionCore(std::move(opts));

    Aws::Crt::String result = IoTSDKMetricsTestHelper::GetEncodedFeatureListForMqtt311(*core);
    ASSERT_TRUE(s_contains(result, "F/3"));
    ASSERT_TRUE(s_contains(result, Aws::Crt::String("G/") + s_socketVal()));
    ASSERT_INT_EQUALS(2, (int)s_partCount(result));
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsMqtt3Minimal, s_TestIoTSDKMetricsMqtt3Minimal)

static int s_TestIoTSDKMetricsDefaultValuesOmitted(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);
    ReconnectOptions ro = {AWS_EXPONENTIAL_BACKOFF_JITTER_DEFAULT, 1000, 1000, 1000};
    options.WithReconnectOptions(ro);
    options.WithSessionBehavior(AWS_MQTT5_CSBT_DEFAULT);
    options.WithOfflineQueueBehavior(AWS_MQTT5_COQBT_DEFAULT);

    Aws::Crt::String result = IoTSDKMetricsTestHelper::GetEncodedFeatureListForMqtt5(options);
    ASSERT_FALSE(s_containsPrefix(result, "A/"));
    ASSERT_FALSE(s_containsPrefix(result, "B/"));
    ASSERT_FALSE(s_containsPrefix(result, "C/"));
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
    Io::TlsContextOptions tlsOpts = Io::TlsContextOptions::InitDefaultClient(allocator);
    tlsOpts.SetMinimumTlsVersion(AWS_IO_TLSv1_2);
    Io::TlsContext tlsCtx(tlsOpts, Io::TlsMode::CLIENT, allocator);
    options.WithTlsConnectionOptions(tlsCtx.NewConnectionOptions());

    Aws::Crt::String result = IoTSDKMetricsTestHelper::GetEncodedFeatureListForMqtt5(options);
    ASSERT_TRUE(s_contains(result, "A/B"));
    ASSERT_TRUE(s_contains(result, "B/A"));
    ASSERT_TRUE(s_contains(result, "C/C"));
    ASSERT_TRUE(s_contains(result, "D/B"));
    ASSERT_TRUE(s_contains(result, "E/A"));
    ASSERT_TRUE(s_contains(result, "F/5"));
    ASSERT_TRUE(s_contains(result, "H/A"));
    ASSERT_TRUE(s_contains(result, "K/D"));
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsAllFeaturesSet, s_TestIoTSDKMetricsAllFeaturesSet)

static int s_TestIoTSDKMetricsMqtt3ProxyTls(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Io::TlsContextOptions tlsOpts = Io::TlsContextOptions::InitDefaultClient(allocator);
    tlsOpts.SetMinimumTlsVersion(AWS_IO_TLSv1_2);
    Io::TlsContext tlsCtx(tlsOpts, Io::TlsMode::CLIENT, allocator);
    Io::TlsConnectionOptions tlsConn = tlsCtx.NewConnectionOptions();
    Io::TlsContextOptions pOpts = Io::TlsContextOptions::InitDefaultClient(allocator);
    Io::TlsContext pCtx(pOpts, Io::TlsMode::CLIENT, allocator);
    Http::HttpClientConnectionProxyOptions p;
    p.HostName = "proxy.example.com";
    p.Port = 443;
    p.ProxyConnectionType = Http::AwsHttpProxyConnectionType::Tunneling;
    p.TlsOptions = tlsConn;

    MqttConnectionOptions opts;
    opts.allocator = allocator;
    opts.hostName = "localhost";
    opts.port = 8883;
    opts.useTls = true;
    opts.tlsConnectionOptions = tlsConn;

    auto core = IoTSDKMetricsTestHelper::CreateTestConnectionCore(std::move(opts));
    core->SetHttpProxyOptions(p);

    Aws::Crt::String result = IoTSDKMetricsTestHelper::GetEncodedFeatureListForMqtt311(*core);
    ASSERT_TRUE(s_contains(result, "F/3"));
    ASSERT_TRUE(s_contains(result, "H/B"));
    ASSERT_TRUE(s_contains(result, "K/D"));
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsMqtt3ProxyTls, s_TestIoTSDKMetricsMqtt3ProxyTls)

//////////////////////////////////////////////////////////
// Merge Feature Lists
//////////////////////////////////////////////////////////

static int s_TestIoTSDKMetricsMergeMultipleOverrides(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    ASSERT_TRUE(IoTSDKMetricsTestHelper::MergeFeatureLists("A/B,F/5,G/A,K/D", "A/C,F/3,K/E") == "A/C,F/3,G/A,K/E");
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsMergeMultipleOverrides, s_TestIoTSDKMetricsMergeMultipleOverrides)

static int s_TestIoTSDKMetricsMergeEmptyUser(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    ASSERT_TRUE(IoTSDKMetricsTestHelper::MergeFeatureLists("F/5,G/A", "") == "F/5,G/A");
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsMergeEmptyUser, s_TestIoTSDKMetricsMergeEmptyUser)

static int s_TestIoTSDKMetricsMergeEmptyCrt(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    ASSERT_TRUE(IoTSDKMetricsTestHelper::MergeFeatureLists("", "A/B") == "A/B");
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsMergeEmptyCrt, s_TestIoTSDKMetricsMergeEmptyCrt)

static int s_TestIoTSDKMetricsMergeBothEmpty(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    ASSERT_TRUE(IoTSDKMetricsTestHelper::MergeFeatureLists("", "") == "");
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsMergeBothEmpty, s_TestIoTSDKMetricsMergeBothEmpty)

//////////////////////////////////////////////////////////
// Create Metrics
//////////////////////////////////////////////////////////

static int s_TestIoTSDKMetricsCreateNullUser(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    IoTDeviceSDKMetrics result = IoTSDKMetricsTestHelper::CreateMetricsFromFeatureList("F/5,G/A", nullptr);
    ASSERT_TRUE(result.LibraryName == "IoTDeviceSDK/CPP");
    ASSERT_FALSE(result.Metadata.at("CRTVersion").empty());
    ASSERT_TRUE(result.Metadata.at("IoTSDKFeature") == "F/5,G/A");
    ASSERT_TRUE(result.Metadata.at("IoTSDKMetricsVersion") == "1");
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsCreateNullUser, s_TestIoTSDKMetricsCreateNullUser)

static int s_TestIoTSDKMetricsCreateUserFeatureAdded(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    IoTDeviceSDKMetrics user;
    user.AddMetadata("IoTSDKMetricsVersion", "1");
    user.AddMetadata("IoTSDKFeature", "I/A");
    IoTDeviceSDKMetrics result = IoTSDKMetricsTestHelper::CreateMetricsFromFeatureList("F/5,G/A", &user);
    Aws::Crt::String features = result.Metadata.at("IoTSDKFeature");
    ASSERT_TRUE(s_contains(features, "I/A"));
    ASSERT_TRUE(s_contains(features, "F/5"));
    ASSERT_TRUE(s_contains(features, "G/A"));
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsCreateUserFeatureAdded, s_TestIoTSDKMetricsCreateUserFeatureAdded)

static int s_TestIoTSDKMetricsCreateUserOverridesCrt(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    IoTDeviceSDKMetrics user;
    user.AddMetadata("IoTSDKMetricsVersion", "1");
    user.AddMetadata("IoTSDKFeature", "F/3,I/B");
    IoTDeviceSDKMetrics result = IoTSDKMetricsTestHelper::CreateMetricsFromFeatureList("F/5,G/A", &user);
    Aws::Crt::String features = result.Metadata.at("IoTSDKFeature");
    ASSERT_TRUE(s_contains(features, "F/3"));
    ASSERT_FALSE(s_contains(features, "F/5"));
    ASSERT_TRUE(s_contains(features, "I/B"));
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsCreateUserOverridesCrt, s_TestIoTSDKMetricsCreateUserOverridesCrt)

static int s_TestIoTSDKMetricsVersionMismatch(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    IoTDeviceSDKMetrics user;
    user.AddMetadata("IoTSDKMetricsVersion", "99");
    user.AddMetadata("IoTSDKFeature", "I/A");
    IoTDeviceSDKMetrics result = IoTSDKMetricsTestHelper::CreateMetricsFromFeatureList("F/5,G/A", &user);
    Aws::Crt::String features = result.Metadata.at("IoTSDKFeature");
    ASSERT_FALSE(s_contains(features, "I/A"));
    ASSERT_TRUE(s_contains(features, "F/5"));
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsVersionMismatch, s_TestIoTSDKMetricsVersionMismatch)

static int s_TestIoTSDKMetricsCRTVersionNotModifiable(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    IoTDeviceSDKMetrics user;
    user.AddMetadata("CRTVersion", "fake_version");
    IoTDeviceSDKMetrics result = IoTSDKMetricsTestHelper::CreateMetricsFromFeatureList("F/5,G/A", &user);
    Aws::Crt::String crtVersion = result.Metadata.at("CRTVersion");
    ASSERT_FALSE(crtVersion.empty());
    ASSERT_FALSE(crtVersion == "fake_version");
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsCRTVersionNotModifiable, s_TestIoTSDKMetricsCRTVersionNotModifiable)

static int s_TestIoTSDKMetricsPreservesUserMetadata(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    IoTDeviceSDKMetrics user;
    user.AddMetadata("IoTSDKVersion", "2.0.0");
    user.AddMetadata("CustomKey", "custom_value");
    IoTDeviceSDKMetrics result = IoTSDKMetricsTestHelper::CreateMetricsFromFeatureList("F/5,G/A", &user);
    ASSERT_TRUE(result.Metadata.at("IoTSDKVersion") == "2.0.0");
    ASSERT_TRUE(result.Metadata.at("CustomKey") == "custom_value");
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsPreservesUserMetadata, s_TestIoTSDKMetricsPreservesUserMetadata)

static int s_TestIoTSDKMetricsCustomLibraryName(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    IoTDeviceSDKMetrics user;
    user.LibraryName = "MyCustomSDK/1.0";
    IoTDeviceSDKMetrics result = IoTSDKMetricsTestHelper::CreateMetricsFromFeatureList("F/5,G/A", &user);
    ASSERT_TRUE(result.LibraryName == "MyCustomSDK/1.0");
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsCustomLibraryName, s_TestIoTSDKMetricsCustomLibraryName)

//////////////////////////////////////////////////////////
// End-to-end: Metrics set via options
//////////////////////////////////////////////////////////

static int s_TestIoTSDKMetricsSetMetricsViaOptions(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    // Create a connection core with custom metrics passed via options
    IoTDeviceSDKMetrics customMetrics;
    customMetrics.LibraryName = "OptionsSDK/1.0";
    customMetrics.AddMetadata("IoTSDKMetricsVersion", "1");
    customMetrics.AddMetadata("IoTSDKFeature", "I/B");

    MqttConnectionOptions opts;
    opts.allocator = allocator;
    opts.hostName = "localhost";
    opts.port = 8883;
    opts.useTls = false;
    opts.sdkMetrics = customMetrics;
    auto core = IoTSDKMetricsTestHelper::CreateTestConnectionCore(std::move(opts));

    // Use createMetricsForMqtt311 to get the final metrics
    IoTDeviceSDKMetrics finalMetrics = IoTSDKMetricsEncoder::createMetricsForMqtt311(*core);

    ASSERT_TRUE(finalMetrics.LibraryName == "OptionsSDK/1.0");
    Aws::Crt::String features = finalMetrics.Metadata.at("IoTSDKFeature");
    ASSERT_TRUE(s_contains(features, "I/B"));
    ASSERT_TRUE(s_contains(features, "F/3"));
    ASSERT_FALSE(finalMetrics.Metadata.at("CRTVersion").empty());

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsSetMetricsViaOptions, s_TestIoTSDKMetricsSetMetricsViaOptions)

static int s_TestIoTSDKMetricsMqtt5WithSdkMetrics(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5ClientOptions options(allocator);
    options.WithHostName("localhost").WithPort(8883);

    // Set custom metrics
    IoTDeviceSDKMetrics customMetrics;
    customMetrics.LibraryName = "Mqtt5TestSDK/2.0";
    customMetrics.AddMetadata("IoTSDKMetricsVersion", "1");
    customMetrics.AddMetadata("IoTSDKFeature", "I/A");
    customMetrics.AddMetadata("AppVersion", "3.0.0");
    options.WithSdkMetrics(std::move(customMetrics));

    // Use createMetricsForMqtt5 to get the final metrics
    IoTDeviceSDKMetrics finalMetrics = IoTSDKMetricsEncoder::createMetricsForMqtt5(options);

    ASSERT_TRUE(finalMetrics.LibraryName == "Mqtt5TestSDK/2.0");
    Aws::Crt::String features = finalMetrics.Metadata.at("IoTSDKFeature");
    ASSERT_TRUE(s_contains(features, "I/A"));
    ASSERT_TRUE(s_contains(features, "F/5"));
    ASSERT_TRUE(finalMetrics.Metadata.at("AppVersion") == "3.0.0");
    ASSERT_FALSE(finalMetrics.Metadata.at("CRTVersion").empty());

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTSDKMetricsMqtt5WithSdkMetrics, s_TestIoTSDKMetricsMqtt5WithSdkMetrics)