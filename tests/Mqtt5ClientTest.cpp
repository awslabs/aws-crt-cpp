/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/common/environment.h>
#include <aws/crt/Api.h>
#include <aws/crt/UUID.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/http/HttpProxyStrategy.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/iot/Mqtt5Client.h>
#include <aws/iot/MqttCommon.h>
#include <aws/testing/aws_test_harness.h>

#include <atomic>
#include <thread>
#include <utility>

using namespace Aws::Crt;
using namespace Aws::Crt::Mqtt5;

//////////////////////////////////////////////////////////
// Creation Test Cases [New-UC] (runs regardless of byo-crypto)
//////////////////////////////////////////////////////////

static void s_setupConnectionLifeCycle(
    Mqtt5::Mqtt5ClientOptions &mqtt5Options,
    std::promise<bool> &connectionPromise,
    std::promise<void> &stoppedPromise,
    const char *clientName = "Client")
{
    mqtt5Options.WithClientConnectionSuccessCallback(
        [&connectionPromise, clientName](const OnConnectionSuccessEventData &)
        {
            printf("[MQTT5]%s Connection Success.\n", clientName);
            connectionPromise.set_value(true);
        });

    mqtt5Options.WithClientConnectionFailureCallback(
        [&connectionPromise, clientName](const OnConnectionFailureEventData &eventData)
        {
            printf(
                "[MQTT5]%s Connection failed with error : %s\n", clientName, aws_error_debug_str(eventData.errorCode));
            connectionPromise.set_value(false);
        });

    mqtt5Options.WithClientStoppedCallback(
        [&stoppedPromise, clientName](const OnStoppedEventData &)
        {
            printf("[MQTT5]%s Stopped\n", clientName);
            stoppedPromise.set_value();
        });
}

/*
 * [New-UC1] Happy path. Minimal creation and cleanup
 */
static int s_TestMqtt5NewClientMin(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Mqtt5::Mqtt5ClientOptions mqtt5Options(allocator);
    // Hardcoded the host name and port for creation test
    mqtt5Options.WithHostName("localhost").WithPort(1883);

    std::shared_ptr<Mqtt5::Mqtt5Client> mqtt5Client = Mqtt5::Mqtt5Client::NewMqtt5Client(mqtt5Options, allocator);
    ASSERT_TRUE(mqtt5Client);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5NewClientMinimal, s_TestMqtt5NewClientMin)

/*
 * [New-UC2] Maximum creation and cleanup
 */
static int s_TestMqtt5NewClientFull(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Mqtt5::Mqtt5ClientOptions mqtt5Options(allocator);
    // Hardcoded the host name and port for creation test
    mqtt5Options.WithHostName("localhost").WithPort(1883);

    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(3000);

    // Setup will
    const Aws::Crt::String TEST_TOPIC = "test/MQTT5_Binding_CPP/s_TestMqtt5NewClientFull";
    ByteBuf will_payload = Aws::Crt::ByteBufFromCString("Will Test");
    std::shared_ptr<Mqtt5::PublishPacket> will = Aws::Crt::MakeShared<Mqtt5::PublishPacket>(
        allocator, TEST_TOPIC, ByteCursorFromByteBuf(will_payload), Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE, allocator);

    std::shared_ptr<Aws::Crt::Mqtt5::ConnectPacket> packetConnect =
        Aws::Crt::MakeShared<Aws::Crt::Mqtt5::ConnectPacket>(allocator);
    packetConnect->WithClientId("s_TestMqtt5DirectConnectionFull" + Aws::Crt::UUID().ToString())
        .WithKeepAliveIntervalSec(1000)
        .WithMaximumPacketSizeBytes(1000L)
        .WithReceiveMaximum(1000)
        .WithRequestProblemInformation(true)
        .WithRequestResponseInformation(true)
        .WithSessionExpiryIntervalSec(1000L)
        .WithWill(will)
        .WithWillDelayIntervalSec(1000);

    Aws::Crt::Mqtt5::UserProperty userProperty("PropertyName", "PropertyValue");
    packetConnect->WithUserProperty(std::move(userProperty));

    Aws::Crt::Mqtt5::ReconnectOptions reconnectOptions = {
        Mqtt5::JitterMode::AWS_EXPONENTIAL_BACKOFF_JITTER_FULL, 1000, 1000, 1000};

    mqtt5Options.WithConnectOptions(packetConnect);
    mqtt5Options.WithBootstrap(ApiHandle::GetOrCreateStaticDefaultClientBootstrap());
    mqtt5Options.WithSocketOptions(socketOptions);
    mqtt5Options.WithSessionBehavior(Mqtt5::ClientSessionBehaviorType::AWS_MQTT5_CSBT_REJOIN_POST_SUCCESS);
    mqtt5Options.WithClientExtendedValidationAndFlowControl(
        Mqtt5::ClientExtendedValidationAndFlowControl::AWS_MQTT5_EVAFCO_NONE);
    mqtt5Options.WithOfflineQueueBehavior(
        Mqtt5::ClientOperationQueueBehaviorType::AWS_MQTT5_COQBT_FAIL_QOS0_PUBLISH_ON_DISCONNECT);
    mqtt5Options.WithReconnectOptions(reconnectOptions);
    mqtt5Options.WithPingTimeoutMs(10000);
    mqtt5Options.WithConnackTimeoutMs(10000);
    mqtt5Options.WithAckTimeoutSec(60000);

    std::promise<bool> connectionPromise;
    std::promise<void> stoppedPromise;

    s_setupConnectionLifeCycle(mqtt5Options, connectionPromise, stoppedPromise);

    std::shared_ptr<Mqtt5::Mqtt5Client> mqtt5Client = Mqtt5::Mqtt5Client::NewMqtt5Client(mqtt5Options, allocator);
    ASSERT_TRUE(mqtt5Client);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5NewClientFull, s_TestMqtt5NewClientFull)

//////////////////////////////////////////////////////////
// Tests that run only without byo-crypto
//////////////////////////////////////////////////////////

#if !BYO_CRYPTO

AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_direct_hostName, "AWS_TEST_MQTT5_DIRECT_MQTT_HOST");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_direct_port, "AWS_TEST_MQTT5_DIRECT_MQTT_PORT");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt5_test_envName_direct_basic_hostName,
    "AWS_TEST_MQTT5_DIRECT_MQTT_BASIC_AUTH_HOST");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_direct_basic_port, "AWS_TEST_MQTT5_DIRECT_MQTT_BASIC_AUTH_PORT");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_direct_tls_hostName, "AWS_TEST_MQTT5_DIRECT_MQTT_TLS_HOST");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_direct_tls_port, "AWS_TEST_MQTT5_DIRECT_MQTT_TLS_PORT");

AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_websocket_hostName, "AWS_TEST_MQTT5_WS_MQTT_HOST");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_websocket_port, "AWS_TEST_MQTT5_WS_MQTT_PORT");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt5_test_envName_websocket_basic_auth_hostName,
    "AWS_TEST_MQTT5_WS_MQTT_BASIC_AUTH_HOST");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt5_test_envName_websocket_basic_auth_port,
    "AWS_TEST_MQTT5_WS_MQTT_BASIC_AUTH_PORT");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_websocket_tls_hostName, "AWS_TEST_MQTT5_WS_MQTT_TLS_HOST");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_websocket_tls_port, "AWS_TEST_MQTT5_WS_MQTT_TLS_PORT");

AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_basic_username, "AWS_TEST_MQTT5_BASIC_AUTH_USERNAME");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_basic_password, "AWS_TEST_MQTT5_BASIC_AUTH_PASSWORD");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_proxy_hostname, "AWS_TEST_MQTT5_PROXY_HOST");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_proxy_port, "AWS_TEST_MQTT5_PROXY_PORT");

AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_certificate, "AWS_TEST_MQTT5_CERTIFICATE_FILE");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_private_key, "AWS_TEST_MQTT5_KEY_FILE");

AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_iot_hostname, "AWS_TEST_MQTT5_IOT_CORE_HOST");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_iot_certificate, "AWS_TEST_MQTT5_IOT_CORE_RSA_CERT");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_iot_key, "AWS_TEST_MQTT5_IOT_CORE_RSA_KEY");

enum Mqtt5TestType
{
    MQTT5CONNECT_DIRECT,
    MQTT5CONNECT_DIRECT_BASIC_AUTH,
    MQTT5CONNECT_DIRECT_TLS,
    MQTT5CONNECT_DIRECT_IOT_CORE,
    MQTT5CONNECT_DIRECT_IOT_CORE_ALPN,
    MQTT5CONNECT_WS,
    MQTT5CONNECT_WS_BASIC_AUTH,
    MQTT5CONNECT_WS_TLS,
    MQTT5CONNECT_WS_IOT_CORE
};

struct Mqtt5TestEnvVars
{
    Mqtt5TestEnvVars(struct aws_allocator *allocator, Mqtt5TestType type)
        : m_error(AWS_OP_SUCCESS), m_allocator(allocator)
    {
        switch (type)
        {
            case MQTT5CONNECT_DIRECT:
            {
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_direct_hostName, &m_hostname);
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_direct_port, &m_port);
                if (m_error != AWS_OP_SUCCESS)
                {
                    return;
                }
                if (m_hostname == NULL || m_port == NULL)
                {
                    m_error = AWS_OP_ERR;
                    return;
                }
                m_hostname_string = aws_string_c_str(m_hostname);
                m_port_value = static_cast<uint32_t>(atoi(aws_string_c_str(m_port)));
                break;
            }
            case MQTT5CONNECT_DIRECT_BASIC_AUTH:
            {
                m_error |=
                    aws_get_environment_value(allocator, s_mqtt5_test_envName_direct_basic_hostName, &m_hostname);
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_direct_basic_port, &m_port);
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_basic_username, &m_username);
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_basic_password, &m_password);

                if (m_error != AWS_OP_SUCCESS)
                {
                    return;
                }
                if (m_hostname == NULL || m_port == NULL || m_username == NULL || m_password == NULL)
                {
                    m_error = AWS_OP_ERR;
                    return;
                }
                m_hostname_string = aws_string_c_str(m_hostname);
                m_port_value = static_cast<uint32_t>(atoi(aws_string_c_str(m_port)));
                m_username_string = aws_string_c_str(m_username);
                m_password_cursor = ByteCursorFromArray(m_password->bytes, m_password->len);
                break;
            }
            case MQTT5CONNECT_DIRECT_TLS:
            {
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_direct_tls_hostName, &m_hostname);
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_direct_tls_port, &m_port);
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_certificate, &m_certificate_path);
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_private_key, &m_private_key_path);

                if (m_error != AWS_OP_SUCCESS)
                {
                    return;
                }
                if (m_hostname == NULL || m_port == NULL || m_certificate_path == NULL || m_private_key_path == NULL)
                {
                    m_error = AWS_OP_ERR;
                    return;
                }
                m_hostname_string = aws_string_c_str(m_hostname);
                m_port_value = static_cast<uint32_t>(atoi(aws_string_c_str(m_port)));
                m_certificate_path_string = aws_string_c_str(m_certificate_path);
                m_private_key_path_string = aws_string_c_str(m_private_key_path);
                break;
            }
            case MQTT5CONNECT_WS:
            {
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_websocket_hostName, &m_hostname);
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_websocket_port, &m_port);

                if (m_error != AWS_OP_SUCCESS)
                {
                    return;
                }
                if (m_hostname == NULL || m_port == NULL)
                {
                    m_error = AWS_OP_ERR;
                    return;
                }
                {
                    m_hostname_string = aws_string_c_str(m_hostname);
                    m_port_value = static_cast<uint32_t>(atoi(aws_string_c_str(m_port)));
                }
                break;
            }
            case MQTT5CONNECT_WS_BASIC_AUTH:
            {
                m_error |= aws_get_environment_value(
                    allocator, s_mqtt5_test_envName_websocket_basic_auth_hostName, &m_hostname);
                m_error |=
                    aws_get_environment_value(allocator, s_mqtt5_test_envName_websocket_basic_auth_port, &m_port);
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_basic_username, &m_username);
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_basic_password, &m_password);

                if (m_error != AWS_OP_SUCCESS)
                {
                    return;
                }
                if (m_hostname == NULL || m_port == NULL || m_username == NULL || m_password == NULL)
                {
                    m_error = AWS_OP_ERR;
                    return;
                }
                {
                    m_hostname_string = aws_string_c_str(m_hostname);
                    m_port_value = static_cast<uint32_t>(atoi(aws_string_c_str(m_port)));
                    m_username_string = aws_string_c_str(m_username);
                    m_password_cursor = ByteCursorFromArray(m_password->bytes, m_password->len);
                }
                break;
            }
            case MQTT5CONNECT_WS_TLS:
            {
                m_error |=
                    aws_get_environment_value(allocator, s_mqtt5_test_envName_websocket_tls_hostName, &m_hostname);
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_websocket_tls_port, &m_port);
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_certificate, &m_certificate_path);
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_private_key, &m_private_key_path);

                if (m_error != AWS_OP_SUCCESS)
                {
                    return;
                }
                if (m_hostname == NULL || m_port == NULL || m_certificate_path == NULL || m_private_key_path == NULL)
                {
                    m_error = AWS_OP_ERR;
                    return;
                }
                {
                    m_hostname_string = aws_string_c_str(m_hostname);
                    m_port_value = static_cast<uint32_t>(atoi(aws_string_c_str(m_port)));
                    m_certificate_path_string = aws_string_c_str(m_certificate_path);
                    m_private_key_path_string = aws_string_c_str(m_private_key_path);
                }
                break;
            }

            case MQTT5CONNECT_DIRECT_IOT_CORE:
            case MQTT5CONNECT_DIRECT_IOT_CORE_ALPN:
            {
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_iot_hostname, &m_hostname);
                m_error |=
                    aws_get_environment_value(allocator, s_mqtt5_test_envName_iot_certificate, &m_certificate_path);
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_iot_key, &m_private_key_path);

                if (m_error != AWS_OP_SUCCESS)
                {
                    return;
                }
                if (m_hostname == NULL || m_certificate_path == NULL || m_private_key_path == NULL)
                {
                    m_error = AWS_OP_ERR;
                    return;
                }
                {
                    m_hostname_string = aws_string_c_str(m_hostname);
                    m_certificate_path_string = aws_string_c_str(m_certificate_path);
                    m_private_key_path_string = aws_string_c_str(m_private_key_path);
                }
                break;
            }

            case MQTT5CONNECT_WS_IOT_CORE:
            {
                m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_iot_hostname, &m_hostname);
                if (m_error != AWS_OP_SUCCESS)
                {
                    return;
                }
                m_hostname_string = aws_string_c_str(m_hostname);
                break;
            }

            default:
                m_error = AWS_OP_ERR;
                break;
        }

        // Setup Http Proxy
        m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_proxy_hostname, &m_httpproxy_hostname);
        m_error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_proxy_port, &m_httpproxy_port);

        if (m_error == AWS_OP_SUCCESS && m_httpproxy_hostname != NULL && m_httpproxy_port != NULL)
        {
            m_httpproxy_hostname_string = aws_string_c_str(m_httpproxy_hostname);
            m_httpproxy_port_value = static_cast<uint32_t>(atoi(aws_string_c_str(m_httpproxy_port)));
        }
    }

    operator bool() const { return m_error == AWS_OP_SUCCESS; }

    ~Mqtt5TestEnvVars()
    {
        if (m_hostname != NULL)
        {
            aws_string_destroy(m_hostname);
            m_hostname = NULL;
        }

        if (m_port != NULL)
        {
            aws_string_destroy(m_port);
            m_port = NULL;
        }

        if (m_username != NULL)
        {
            aws_string_destroy(m_username);
            m_username = NULL;
        }

        if (m_password != NULL)
        {
            aws_string_destroy(m_password);
            m_password = NULL;
        }

        if (m_certificate_path != NULL)
        {
            aws_string_destroy(m_certificate_path);
            m_certificate_path = NULL;
        }

        if (m_private_key_path != NULL)
        {
            aws_string_destroy(m_private_key_path);
            m_private_key_path = NULL;
        }

        if (m_httpproxy_hostname != NULL)
        {
            aws_string_destroy(m_httpproxy_hostname);
            m_httpproxy_hostname = NULL;
        }

        if (m_httpproxy_port != NULL)
        {
            aws_string_destroy(m_httpproxy_port);
            m_httpproxy_port = NULL;
        }
    }

    int m_error;
    struct aws_allocator *m_allocator;

    struct aws_string *m_hostname = NULL;
    struct aws_string *m_port = NULL;
    struct aws_string *m_username = NULL;
    struct aws_string *m_password = NULL;
    struct aws_string *m_certificate_path = NULL;
    struct aws_string *m_private_key_path = NULL;
    struct aws_string *m_httpproxy_hostname = NULL;
    struct aws_string *m_httpproxy_port = NULL;

    Aws::Crt::String m_hostname_string;
    uint32_t m_port_value;
    Aws::Crt::String m_username_string;
    Aws::Crt::ByteCursor m_password_cursor;
    Aws::Crt::String m_certificate_path_string;
    Aws::Crt::String m_private_key_path_string;
    Aws::Crt::String m_httpproxy_hostname_string;
    uint32_t m_httpproxy_port_value;
};

//////////////////////////////////////////////////////////
// Test Helper
//////////////////////////////////////////////////////////

struct Mqtt5TestContext
{
    int testDirective;
    std::shared_ptr<Mqtt5Client> client;
    std::promise<bool> connectionPromise;
    std::promise<void> stoppedPromise;
};

static Mqtt5TestContext createTestContext(
    struct aws_allocator *allocator,
    enum Mqtt5TestType testType,
    std::function<int(Mqtt5ClientOptions &, const Mqtt5TestEnvVars &, Mqtt5TestContext &)> configMutator = {})
{
    struct Mqtt5TestContext context;
    context.testDirective = AWS_OP_SKIP;

    Mqtt5TestEnvVars mqtt5TestVars(allocator, testType);
    if (!mqtt5TestVars)
    {
        return context;
    }

    Mqtt5ClientOptions mqtt5Options(allocator);
    mqtt5Options.WithHostName(mqtt5TestVars.m_hostname_string);
    mqtt5Options.WithPort(mqtt5TestVars.m_port_value);

    s_setupConnectionLifeCycle(mqtt5Options, context.connectionPromise, context.stoppedPromise);

    switch (testType)
    {
        case MQTT5CONNECT_DIRECT_BASIC_AUTH:
        {
            std::shared_ptr<ConnectPacket> packetConnect = Aws::Crt::MakeShared<ConnectPacket>(allocator);
            packetConnect->WithUserName(mqtt5TestVars.m_username_string);
            packetConnect->WithPassword(mqtt5TestVars.m_password_cursor);
            mqtt5Options.WithConnectOptions(packetConnect);
            break;
        }

        case MQTT5CONNECT_DIRECT_TLS:
        {
            Io::TlsContextOptions tlsCtxOptions = Io::TlsContextOptions::InitDefaultClient();
            tlsCtxOptions.SetVerifyPeer(false);
            Io::TlsContext tlsContext(tlsCtxOptions, Io::TlsMode::CLIENT, allocator);
            Io::TlsConnectionOptions tlsConnection = tlsContext.NewConnectionOptions();
            mqtt5Options.WithTlsConnectionOptions(tlsConnection);
            break;
        }

        case MQTT5CONNECT_DIRECT_IOT_CORE:
        {
            mqtt5Options.WithPort(8883);

            Io::TlsContextOptions tlsCtxOptions = Io::TlsContextOptions::InitClientWithMtls(
                mqtt5TestVars.m_certificate_path_string.c_str(),
                mqtt5TestVars.m_private_key_path_string.c_str(),
                allocator);

            Io::TlsContext tlsContext(tlsCtxOptions, Io::TlsMode::CLIENT, allocator);
            Io::TlsConnectionOptions tlsConnection = tlsContext.NewConnectionOptions();
            mqtt5Options.WithTlsConnectionOptions(tlsConnection);
            break;
        }

        case MQTT5CONNECT_DIRECT_IOT_CORE_ALPN:
        {
            mqtt5Options.WithPort(443);

            Io::TlsContextOptions tlsCtxOptions = Io::TlsContextOptions::InitClientWithMtls(
                mqtt5TestVars.m_certificate_path_string.c_str(),
                mqtt5TestVars.m_private_key_path_string.c_str(),
                allocator);

            Io::TlsContext tlsContext(tlsCtxOptions, Io::TlsMode::CLIENT, allocator);
            Io::TlsConnectionOptions tlsConnection = tlsContext.NewConnectionOptions();
            tlsConnection.SetAlpnList("x-amzn-mqtt-ca");
            mqtt5Options.WithTlsConnectionOptions(tlsConnection);
            break;
        }

        case MQTT5CONNECT_WS:
        {
            mqtt5Options.WithWebsocketHandshakeTransformCallback(
                [](std::shared_ptr<Aws::Crt::Http::HttpRequest> req,
                   const Aws::Crt::Mqtt5::OnWebSocketHandshakeInterceptComplete &onComplete)
                { onComplete(req, AWS_ERROR_SUCCESS); });

            break;
        }

        case MQTT5CONNECT_WS_BASIC_AUTH:
        {
            mqtt5Options.WithWebsocketHandshakeTransformCallback(
                [](std::shared_ptr<Aws::Crt::Http::HttpRequest> req,
                   const Aws::Crt::Mqtt5::OnWebSocketHandshakeInterceptComplete &onComplete)
                { onComplete(req, AWS_ERROR_SUCCESS); });

            std::shared_ptr<ConnectPacket> packetConnect = Aws::Crt::MakeShared<ConnectPacket>(allocator);
            packetConnect->WithUserName(mqtt5TestVars.m_username_string);
            packetConnect->WithPassword(mqtt5TestVars.m_password_cursor);
            mqtt5Options.WithConnectOptions(packetConnect);
            break;
        }

        case MQTT5CONNECT_WS_TLS:
        {
            mqtt5Options.WithWebsocketHandshakeTransformCallback(
                [](std::shared_ptr<Aws::Crt::Http::HttpRequest> req,
                   const Aws::Crt::Mqtt5::OnWebSocketHandshakeInterceptComplete &onComplete)
                { onComplete(req, AWS_ERROR_SUCCESS); });

            Io::TlsContextOptions tlsCtxOptions = Io::TlsContextOptions::InitDefaultClient();
            tlsCtxOptions.SetVerifyPeer(false);
            Io::TlsContext tlsContext(tlsCtxOptions, Io::TlsMode::CLIENT, allocator);
            Io::TlsConnectionOptions tlsConnection = tlsContext.NewConnectionOptions();
            mqtt5Options.WithTlsConnectionOptions(tlsConnection);
            break;
        }

        case MQTT5CONNECT_WS_IOT_CORE:
        {
            mqtt5Options.WithPort(443);

            Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();

            Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
            Aws::Crt::Io::TlsConnectionOptions tlsConnection = tlsContext.NewConnectionOptions();
            mqtt5Options.WithTlsConnectionOptions(tlsConnection);

            // setup websocket config
            Aws::Crt::Auth::CredentialsProviderChainDefaultConfig defaultConfig;
            std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider =
                Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(defaultConfig);

            Aws::Iot::WebsocketConfig config("us-east-1", provider);

            mqtt5Options.WithWebsocketHandshakeTransformCallback(
                [config](
                    std::shared_ptr<Aws::Crt::Http::HttpRequest> req,
                    const Aws::Crt::Mqtt5::OnWebSocketHandshakeInterceptComplete &onComplete)
                {
                    auto signingComplete =
                        [onComplete](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &req1, int errorCode)
                    { onComplete(req1, errorCode); };

                    auto signerConfig = config.CreateSigningConfigCb();

                    config.Signer->SignRequest(req, *signerConfig, signingComplete);
                });
            break;
        }

        default:
            break;
    }

    if (configMutator)
    {
        if (configMutator(mqtt5Options, mqtt5TestVars, context) == AWS_OP_SKIP)
        {
            return context;
        }
    }

    context.client = Mqtt5Client::NewMqtt5Client(mqtt5Options, allocator);
    context.testDirective = AWS_OP_SUCCESS;

    return context;
}

//////////////////////////////////////////////////////////
// Direct Connect Test Cases [ConnDC-UC]
//////////////////////////////////////////////////////////

/*
 * [ConnDC-UC1] Happy path. Direct connection with minimal configuration
 */
static int s_TestMqtt5DirectConnectionMinimal(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);
    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5DirectConnectionMinimal, s_TestMqtt5DirectConnectionMinimal)

/*
 * [ConnDC-UC2] Direct connection with basic authentication
 */
static int s_TestMqtt5DirectConnectionWithBasicAuth(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_BASIC_AUTH);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5DirectConnectionWithBasicAuth, s_TestMqtt5DirectConnectionWithBasicAuth)

/*
 * [ConnDC-UC3] Direct connection with TLS
 */
static int s_TestMqtt5DirectConnectionWithTLS(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_TLS);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);
    ASSERT_TRUE(mqtt5Client->Start());
    testContext.connectionPromise.get_future().get();
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5DirectConnectionWithTLS, s_TestMqtt5DirectConnectionWithTLS)

/*
 * [ConnDC-UC4] Direct connection with mutual TLS
 */
static int s_TestMqtt5DirectConnectionWithMutualTLS(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);
    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5DirectConnectionWithMutualTLS, s_TestMqtt5DirectConnectionWithMutualTLS)

/*
 * Direct connection with mutual TLS and ALPN
 */
static int s_TestMqtt5DirectConnectionWithMutualTLSAndALPN(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE_ALPN);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);
    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5DirectConnectionWithMutualTLSAndALPN, s_TestMqtt5DirectConnectionWithMutualTLSAndALPN)

static int s_applyTunnelingProxyToClientOptions(
    Mqtt5ClientOptions &options,
    const Mqtt5TestEnvVars &mqtt5TestVars,
    Mqtt5TestContext &)
{
    if (!mqtt5TestVars.m_httpproxy_hostname || mqtt5TestVars.m_httpproxy_hostname->len == 0)
    {
        return AWS_OP_SKIP;
    }

    Aws::Crt::Http::HttpClientConnectionProxyOptions proxyOptions;
    proxyOptions.HostName = mqtt5TestVars.m_httpproxy_hostname_string;
    proxyOptions.Port = mqtt5TestVars.m_httpproxy_port_value;
    proxyOptions.ProxyConnectionType = Aws::Crt::Http::AwsHttpProxyConnectionType::Tunneling;
    options.WithHttpProxyOptions(proxyOptions);

    return AWS_OP_SUCCESS;
}

/*
 * [ConnDC-UC5] Direct connection with HttpProxy options
 */
static int s_TestMqtt5DirectConnectionWithHttpProxy(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext =
        createTestContext(allocator, MQTT5CONNECT_DIRECT_TLS, s_applyTunnelingProxyToClientOptions);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5DirectConnectionWithHttpProxy, s_TestMqtt5DirectConnectionWithHttpProxy)

static int s_setAllClientOptions(Aws::Crt::Allocator *allocator, Mqtt5ClientOptions &mqtt5Options)
{
    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(3000);

    // Setup will
    const Aws::Crt::String TEST_TOPIC =
        "test/MQTT5_Binding_CPP/s_TestMqtt5DirectConnectionFull" + Aws::Crt::UUID().ToString();
    ByteBuf will_payload = Aws::Crt::ByteBufFromCString("Will Test");
    std::shared_ptr<Mqtt5::PublishPacket> will = Aws::Crt::MakeShared<Mqtt5::PublishPacket>(
        allocator, TEST_TOPIC, ByteCursorFromByteBuf(will_payload), Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE, allocator);

    std::shared_ptr<Aws::Crt::Mqtt5::ConnectPacket> packetConnect =
        Aws::Crt::MakeShared<Aws::Crt::Mqtt5::ConnectPacket>(allocator);
    packetConnect->WithClientId("s_TestMqtt5DirectConnectionFull" + Aws::Crt::UUID().ToString())
        .WithKeepAliveIntervalSec(1000)
        .WithMaximumPacketSizeBytes(1000L)
        .WithReceiveMaximum(1000)
        .WithRequestProblemInformation(true)
        .WithRequestResponseInformation(true)
        .WithSessionExpiryIntervalSec(1000L)
        .WithWill(will)
        .WithWillDelayIntervalSec(1000);

    Aws::Crt::Mqtt5::UserProperty userProperty("PropertyName", "PropertyValue");
    packetConnect->WithUserProperty(std::move(userProperty));

    Aws::Crt::Mqtt5::ReconnectOptions reconnectOptions = {
        Mqtt5::JitterMode::AWS_EXPONENTIAL_BACKOFF_JITTER_FULL, 1000, 1000, 1000};

    mqtt5Options.WithConnectOptions(packetConnect);
    mqtt5Options.WithBootstrap(ApiHandle::GetOrCreateStaticDefaultClientBootstrap());
    mqtt5Options.WithSocketOptions(socketOptions);
    mqtt5Options.WithSessionBehavior(Mqtt5::ClientSessionBehaviorType::AWS_MQTT5_CSBT_REJOIN_POST_SUCCESS);
    mqtt5Options.WithClientExtendedValidationAndFlowControl(
        Mqtt5::ClientExtendedValidationAndFlowControl::AWS_MQTT5_EVAFCO_NONE);
    mqtt5Options.WithOfflineQueueBehavior(
        Mqtt5::ClientOperationQueueBehaviorType::AWS_MQTT5_COQBT_FAIL_QOS0_PUBLISH_ON_DISCONNECT);
    mqtt5Options.WithReconnectOptions(reconnectOptions);
    mqtt5Options.WithPingTimeoutMs(10000);
    mqtt5Options.WithConnackTimeoutMs(10000);
    mqtt5Options.WithAckTimeoutSec(60000);

    return AWS_OP_SUCCESS;
}

/*
 * [ConnDC-UC6] Direct connection with all options set
 */
static int s_TestMqtt5DirectConnectionFull(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT,
        [allocator](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &)
        { return s_setAllClientOptions(allocator, options); });

    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5DirectConnectionFull, s_TestMqtt5DirectConnectionFull)

//////////////////////////////////////////////////////////
// Websocket Connect Test Cases [ConnWS-UC]
//////////////////////////////////////////////////////////

/*
 * [ConnWS-UC1] Happy path. Websocket connection with minimal configuration.
 */
static int s_TestMqtt5WSConnectionMinimal(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_WS);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5WSConnectionMinimal, s_TestMqtt5WSConnectionMinimal)

/*
 * [ConnWS-UC2] websocket connection with basic authentication
 */
static int s_TestMqtt5WSConnectionWithBasicAuth(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_WS_BASIC_AUTH);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5WSConnectionWithBasicAuth, s_TestMqtt5WSConnectionWithBasicAuth)

/*
 * [ConnWS-UC3] websocket connection with TLS
 */
static int s_TestMqtt5WSConnectionWithTLS(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_WS_TLS);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    testContext.connectionPromise.get_future().get();
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5WSConnectionWithTLS, s_TestMqtt5WSConnectionWithTLS)

/*
 * [ConnDC-UC4] Websocket connection with IoT Core
 */
static int s_TestMqtt5WSConnectionWithMutualTLS(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_WS_IOT_CORE);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    testContext.connectionPromise.get_future().get();
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5WSConnectionWithMutualTLS, s_TestMqtt5WSConnectionWithMutualTLS)

/*
 * ConnWS-UC5] Websocket connection with HttpProxy options
 */
static int s_TestMqtt5WSConnectionWithHttpProxy(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext =
        createTestContext(allocator, MQTT5CONNECT_WS_IOT_CORE, s_applyTunnelingProxyToClientOptions);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5WSConnectionWithHttpProxy, s_TestMqtt5WSConnectionWithHttpProxy)

/*
 * [ConnDC-UC6] Websocket connection with all options set
 */
static int s_TestMqtt5WSConnectionFull(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(
        allocator,
        MQTT5CONNECT_WS_IOT_CORE,
        [allocator](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &)
        { return s_setAllClientOptions(allocator, options); });
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5WSConnectionFull, s_TestMqtt5WSConnectionFull)

////////////////////////////////////////////////////////////
// Negative Connect Tests with Incorrect Data [ConnNegativeID-UC]
////////////////////////////////////////////////////////////

/*
 * [ConnNegativeID-UC1] Client connect with invalid host name
 */
static int s_TestMqtt5DirectInvalidHostname(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT_IOT_CORE,
        [](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &)
        {
            options.WithHostName("invalid");
            return AWS_OP_SUCCESS;
        });
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_FALSE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5InvalidHostname, s_TestMqtt5DirectInvalidHostname)

/*
 * [ConnNegativeID-UC2] Client connect with invalid port for direct connection
 */
static int s_TestMqtt5DirectInvalidPort(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT,
        [](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &)
        {
            options.WithPort(8080);
            return AWS_OP_SUCCESS;
        });
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_FALSE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5InvalidPort, s_TestMqtt5DirectInvalidPort)

/*
 * [ConnNegativeID-UC3] Client connect with invalid port for websocket connection
 */
static int s_TestMqtt5WSInvalidPort(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(
        allocator,
        MQTT5CONNECT_WS,
        [](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &)
        {
            options.WithPort(8883);
            return AWS_OP_SUCCESS;
        });
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_FALSE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5WSInvalidPort, s_TestMqtt5WSInvalidPort)

/*
 * [ConnNegativeID-UC5] Client connect with incorrect basic authentication credentials
 */
static int s_TestMqtt5IncorrectBasicAuth(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT_BASIC_AUTH,
        [allocator](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &)
        {
            std::shared_ptr<Aws::Crt::Mqtt5::ConnectPacket> packetConnect =
                Aws::Crt::MakeShared<Aws::Crt::Mqtt5::ConnectPacket>(allocator);
            packetConnect->WithUserName("WRONG_USERNAME");
            packetConnect->WithPassword(ByteCursorFromCString("WRONG_PASSWORD"));
            options.WithConnectOptions(packetConnect);
            return AWS_OP_SUCCESS;
        });
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_FALSE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5IncorrectBasicAuth, s_TestMqtt5IncorrectBasicAuth)

// [ConnNegativeID-UC6] Client Websocket Handshake Failure test
static int s_TestMqtt5IncorrectWSConnect(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT_BASIC_AUTH,
        [](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &)
        {
            options.WithWebsocketHandshakeTransformCallback(
                [](std::shared_ptr<Aws::Crt::Http::HttpRequest> req,
                   const Aws::Crt::Mqtt5::OnWebSocketHandshakeInterceptComplete &onComplete)
                { onComplete(req, AWS_ERROR_UNSUPPORTED_OPERATION); });
            return AWS_OP_SUCCESS;
        });
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_FALSE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5IncorrectWSConnect, s_TestMqtt5IncorrectWSConnect)

/*
 * [ConnNegativeID-UC7] Double Client ID Failure test
 */
static int s_TestMqtt5DoubleClientIDFailure(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    std::shared_ptr<Aws::Crt::Mqtt5::ConnectPacket> packetConnect =
        Aws::Crt::MakeShared<Aws::Crt::Mqtt5::ConnectPacket>(allocator);
    packetConnect->WithClientId("TestMqtt5DoubleClientIDFailure" + Aws::Crt::UUID().ToString());
    std::promise<void> disconnectPromise;

    Mqtt5TestContext testContext1 = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT_IOT_CORE,
        [packetConnect, &disconnectPromise](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &)
        {
            options.WithConnectOptions(packetConnect);
            options.WithClientDisconnectionCallback([&disconnectPromise](const OnDisconnectionEventData &)
                                                    { disconnectPromise.set_value(); });

            return AWS_OP_SUCCESS;
        });
    if (testContext1.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client1 = testContext1.client;
    ASSERT_TRUE(mqtt5Client1);

    Mqtt5TestContext testContext2 = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT_IOT_CORE,
        [packetConnect](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &)
        {
            options.WithConnectOptions(packetConnect);
            return AWS_OP_SUCCESS;
        });
    if (testContext2.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client2 = testContext2.client;
    ASSERT_TRUE(mqtt5Client2);

    ASSERT_TRUE(mqtt5Client1->Start());
    // Client 1 is connected.
    ASSERT_TRUE(testContext1.connectionPromise.get_future().get());

    // delay to reduce chance of eventual consistency issues causing the second connection to be rejected
    std::this_thread::sleep_for(std::chrono::seconds(3));

    ASSERT_TRUE(mqtt5Client2->Start());

    // Make sure the client2 is connected.
    ASSERT_TRUE(testContext2.connectionPromise.get_future().get());

    // Client 1 should get diconnected.
    disconnectPromise.get_future().get();
    // reset the promise so it would not get confused when we stop the client;
    disconnectPromise = std::promise<void>();

    ASSERT_TRUE(mqtt5Client2->Stop());
    testContext2.stoppedPromise.get_future().get();
    ASSERT_TRUE(mqtt5Client1->Stop());
    testContext1.stoppedPromise.get_future().get();

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5DoubleClientIDFailure, s_TestMqtt5DoubleClientIDFailure)

//////////////////////////////////////////////////////////
// Negative Data Input Tests [NewNegativePK-UC] - Ignored, not applied to C++
//////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////
// Negotiated Settings Tests [Negotiated-UC]
//////////////////////////////////////////////////////////

/*
 * [Negotiated-UC1] Happy path, minimal success test
 */
static int s_TestMqtt5NegotiatedSettingsHappy(Aws::Crt::Allocator *allocator, void *)
{
    const uint32_t SESSION_EXPIRY_INTERVAL_SEC = 600;

    ApiHandle apiHandle(allocator);

    Mqtt5TestContext testContext = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT_IOT_CORE,
        [&](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &context)
        {
            std::shared_ptr<Aws::Crt::Mqtt5::ConnectPacket> packetConnect =
                Aws::Crt::MakeShared<Aws::Crt::Mqtt5::ConnectPacket>(allocator);
            packetConnect->WithSessionExpiryIntervalSec(SESSION_EXPIRY_INTERVAL_SEC);
            options.WithConnectOptions(packetConnect);

            options.WithClientConnectionSuccessCallback(
                [&](const OnConnectionSuccessEventData &eventData)
                {
                    ASSERT_TRUE(
                        eventData.negotiatedSettings->getSessionExpiryIntervalSec() == SESSION_EXPIRY_INTERVAL_SEC);
                    context.connectionPromise.set_value(true);
                    return 0;
                });

            return AWS_OP_SUCCESS;
        });
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5NegotiatedSettingsHappy, s_TestMqtt5NegotiatedSettingsHappy)

/*
 * [Negotiated-UC2] maximum success test
 */
static int s_TestMqtt5NegotiatedSettingsFull(Aws::Crt::Allocator *allocator, void *)
{
    const String CLIENT_ID = "s_TestMqtt5NegotiatedSettingsFull" + Aws::Crt::UUID().ToString();
    const uint32_t SESSION_EXPIRY_INTERVAL_SEC = 600;
    const uint16_t RECEIVE_MAX = 12;
    const uint16_t KEEP_ALIVE_INTERVAL = 1000;

    ApiHandle apiHandle(allocator);

    Mqtt5TestContext testContext = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT_IOT_CORE,
        [&](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &context)
        {
            std::shared_ptr<Aws::Crt::Mqtt5::ConnectPacket> packetConnect =
                Aws::Crt::MakeShared<Aws::Crt::Mqtt5::ConnectPacket>(allocator);
            packetConnect->WithSessionExpiryIntervalSec(SESSION_EXPIRY_INTERVAL_SEC)
                .WithClientId(CLIENT_ID)
                .WithReceiveMaximum(RECEIVE_MAX)
                .WithMaximumPacketSizeBytes(UINT32_MAX)
                .WithKeepAliveIntervalSec(KEEP_ALIVE_INTERVAL);
            options.WithConnectOptions(packetConnect);

            options.WithClientConnectionSuccessCallback(
                [&](const OnConnectionSuccessEventData &eventData)
                {
                    std::shared_ptr<NegotiatedSettings> settings = eventData.negotiatedSettings;
                    ASSERT_TRUE(settings->getSessionExpiryIntervalSec() == SESSION_EXPIRY_INTERVAL_SEC);
                    ASSERT_TRUE(settings->getClientId() == CLIENT_ID);
                    ASSERT_TRUE(settings->getServerKeepAliveSec() == KEEP_ALIVE_INTERVAL);

                    context.connectionPromise.set_value(true);
                    return 0;
                });

            return AWS_OP_SUCCESS;
        });
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5NegotiatedSettingsFull, s_TestMqtt5NegotiatedSettingsFull)

/*
 * [Negotiated-UC3] Server Settings limit test
 */
static int s_TestMqtt5NegotiatedSettingsLimit(Aws::Crt::Allocator *allocator, void *)
{
    const uint32_t SESSION_EXPIRY_INTERVAL_SEC = UINT32_MAX;
    const uint16_t RECEIVE_MAX = UINT16_MAX;
    const uint16_t KEEP_ALIVE_INTERVAL = UINT16_MAX;
    const uint32_t PACKET_MAX = UINT32_MAX;

    ApiHandle apiHandle(allocator);

    Mqtt5TestContext testContext = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT_IOT_CORE,
        [&](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &context)
        {
            std::shared_ptr<Aws::Crt::Mqtt5::ConnectPacket> packetConnect =
                Aws::Crt::MakeShared<Aws::Crt::Mqtt5::ConnectPacket>(allocator);
            packetConnect->WithSessionExpiryIntervalSec(SESSION_EXPIRY_INTERVAL_SEC)
                .WithReceiveMaximum(RECEIVE_MAX)
                .WithMaximumPacketSizeBytes(UINT32_MAX)
                .WithKeepAliveIntervalSec(KEEP_ALIVE_INTERVAL);
            options.WithConnectOptions(packetConnect);

            options.WithClientConnectionSuccessCallback(
                [&](const OnConnectionSuccessEventData &eventData)
                {
                    std::shared_ptr<NegotiatedSettings> settings = eventData.negotiatedSettings;
                    uint16_t receivedmax = settings->getReceiveMaximumFromServer();
                    uint32_t max_package = settings->getMaximumPacketSizeToServer();
                    ASSERT_FALSE(receivedmax == RECEIVE_MAX);
                    ASSERT_FALSE(max_package == PACKET_MAX);
                    ASSERT_FALSE(settings->getRejoinedSession());

                    context.connectionPromise.set_value(true);
                    return 0;
                });

            return AWS_OP_SUCCESS;
        });
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5NegotiatedSettingsLimit, s_TestMqtt5NegotiatedSettingsLimit)

/*
 * [Negotiated-UC4] Rejoin Always Session Behavior
 */
static int s_TestMqtt5NegotiatedSettingsRejoinAlways(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    static const uint32_t SESSION_EXPIRY_INTERVAL_SEC = 3600;

    std::shared_ptr<Aws::Crt::Mqtt5::ConnectPacket> packetConnect =
        Aws::Crt::MakeShared<Aws::Crt::Mqtt5::ConnectPacket>(allocator);
    packetConnect->WithSessionExpiryIntervalSec(SESSION_EXPIRY_INTERVAL_SEC);
    packetConnect->WithClientId(Aws::Crt::UUID().ToString());

    Mqtt5TestContext testContext1 = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT_IOT_CORE,
        [packetConnect](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &context)
        {
            options.WithConnectOptions(packetConnect);
            options.WithClientConnectionSuccessCallback(
                [&](const OnConnectionSuccessEventData &eventData)
                {
                    std::shared_ptr<NegotiatedSettings> settings = eventData.negotiatedSettings;
                    ASSERT_FALSE(settings->getRejoinedSession());

                    context.connectionPromise.set_value(true);
                    return 0;
                });

            return AWS_OP_SUCCESS;
        });
    if (testContext1.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client1 = testContext1.client;
    ASSERT_TRUE(mqtt5Client1);

    ASSERT_TRUE(mqtt5Client1->Start());
    ASSERT_TRUE(testContext1.connectionPromise.get_future().get());

    ASSERT_TRUE(mqtt5Client1->Stop());
    testContext1.stoppedPromise.get_future().get();

    // avoid eventual consistency issues with the follow-up connection (sometimes gets rejected)
    std::this_thread::sleep_for(std::chrono::seconds(3));

    Mqtt5TestContext testContext2 = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT_IOT_CORE,
        [packetConnect](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &context)
        {
            options.WithConnectOptions(packetConnect);
            options.WithClientConnectionSuccessCallback(
                [&](const OnConnectionSuccessEventData &eventData)
                {
                    std::shared_ptr<NegotiatedSettings> settings = eventData.negotiatedSettings;
                    ASSERT_TRUE(settings->getRejoinedSession());

                    context.connectionPromise.set_value(true);
                    return 0;
                });
            options.WithSessionBehavior(Aws::Crt::Mqtt5::ClientSessionBehaviorType::AWS_MQTT5_CSBT_REJOIN_ALWAYS);

            return AWS_OP_SUCCESS;
        });

    std::shared_ptr<Mqtt5Client> mqtt5Client2 = testContext2.client;
    ASSERT_TRUE(mqtt5Client2);

    ASSERT_TRUE(mqtt5Client2->Start());
    ASSERT_TRUE(testContext2.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client2->Stop());
    testContext2.stoppedPromise.get_future().get();

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5NegotiatedSettingsRejoinAlways, s_TestMqtt5NegotiatedSettingsRejoinAlways)

//////////////////////////////////////////////////////////
// Operation Tests [Op-UC]
//////////////////////////////////////////////////////////

/*
 * [Op-UC1] Sub-Unsub happy path
 */
static int s_TestMqtt5SubUnsub(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    int receivedCount = 0;
    std::mutex receivedLock;
    std::condition_variable receivedSignal;
    const String TEST_TOPIC = "test/MQTT5_Binding_CPP" + Aws::Crt::UUID().ToString();

    Mqtt5TestContext testContext = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT_IOT_CORE,
        [&receivedLock, &receivedCount, &receivedSignal, &TEST_TOPIC](
            Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &context)
        {
            options.WithPublishReceivedCallback(
                [&receivedLock, &receivedCount, &receivedSignal, &TEST_TOPIC](const PublishReceivedEventData &eventData)
                {
                    String topic = eventData.publishPacket->getTopic();
                    if (topic == TEST_TOPIC)
                    {
                        std::lock_guard<std::mutex> lock(receivedLock);
                        ++receivedCount;
                        receivedSignal.notify_one();
                    }
                });

            return AWS_OP_SUCCESS;
        });
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());

    /* Subscribe to test topic */
    std::promise<std::shared_ptr<SubAckPacket>> subscribed;
    Mqtt5::Subscription subscription(TEST_TOPIC, Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE, allocator);
    subscription.WithNoLocal(false);
    std::shared_ptr<Mqtt5::SubscribePacket> subscribe =
        Aws::Crt::MakeShared<Mqtt5::SubscribePacket>(allocator, allocator);
    subscribe->WithSubscription(std::move(subscription));
    ASSERT_TRUE(mqtt5Client->Subscribe(
        subscribe,
        [&subscribed](int errorCode, std::shared_ptr<SubAckPacket> suback) { subscribed.set_value(suback); }));
    const auto &suback = subscribed.get_future().get();
    ASSERT_NOT_NULL(suback.get());

    /* Publish message 1 to test topic */
    ByteBuf payload = Aws::Crt::ByteBufFromCString("Hello World");
    std::shared_ptr<Mqtt5::PublishPacket> publish = Aws::Crt::MakeShared<Mqtt5::PublishPacket>(
        allocator, TEST_TOPIC, ByteCursorFromByteBuf(payload), Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE, allocator);
    ASSERT_TRUE(mqtt5Client->Publish(publish));

    {
        std::unique_lock<std::mutex> lock(receivedLock);
        receivedSignal.wait(lock, [&receivedCount]() -> bool { return receivedCount >= 1; });
    }

    std::promise<std::shared_ptr<UnSubAckPacket>> unsubscribed;
    Vector<String> topics;
    topics.push_back(TEST_TOPIC);
    std::shared_ptr<Mqtt5::UnsubscribePacket> unsub =
        Aws::Crt::MakeShared<Mqtt5::UnsubscribePacket>(allocator, allocator);
    unsub->WithTopicFilters(topics);
    ASSERT_TRUE(mqtt5Client->Unsubscribe(
        unsub,
        [&unsubscribed](int errorCode, std::shared_ptr<UnSubAckPacket> unsuback)
        { unsubscribed.set_value(unsuback); }));
    unsubscribed.get_future().get();

    /* Publish message2 to test topic */
    ASSERT_TRUE(mqtt5Client->Publish(publish));

    // Sleep and wait
    aws_thread_current_sleep(2000 * 1000 * 1000);

    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();

    {
        std::lock_guard<std::mutex> finalLock(receivedLock);
        ASSERT_TRUE(receivedCount == 1);
    }

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5SubUnsub, s_TestMqtt5SubUnsub)

/*
 * [Op-UC2] Will test
 */
static int s_TestMqtt5WillTest(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    bool receivedWill = false;
    std::mutex receivedLock;
    std::condition_variable receivedSignal;
    const String TEST_TOPIC = "test/MQTT5_Binding_CPP" + Aws::Crt::UUID().ToString();

    Mqtt5TestContext subscriberContext = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT_IOT_CORE,
        [&receivedLock, &receivedWill, &receivedSignal, &TEST_TOPIC](
            Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &context)
        {
            options.WithPublishReceivedCallback(
                [&receivedLock, &receivedWill, &receivedSignal, &TEST_TOPIC](const PublishReceivedEventData &eventData)
                {
                    String topic = eventData.publishPacket->getTopic();
                    if (topic == TEST_TOPIC)
                    {
                        std::lock_guard<std::mutex> lock(receivedLock);
                        receivedWill = true;
                        receivedSignal.notify_one();
                    }
                });

            return AWS_OP_SUCCESS;
        });
    if (subscriberContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> subscriberClient = subscriberContext.client;
    ASSERT_TRUE(subscriberClient);

    Mqtt5TestContext publisherContext = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT_IOT_CORE,
        [allocator, &TEST_TOPIC](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &)
        {
            std::shared_ptr<Aws::Crt::Mqtt5::ConnectPacket> packetConnect =
                Aws::Crt::MakeShared<Aws::Crt::Mqtt5::ConnectPacket>(allocator);
            ByteBuf will_payload = Aws::Crt::ByteBufFromCString("Will Test");
            std::shared_ptr<Mqtt5::PublishPacket> will = Aws::Crt::MakeShared<Mqtt5::PublishPacket>(
                allocator,
                TEST_TOPIC,
                ByteCursorFromByteBuf(will_payload),
                Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE,
                allocator);
            packetConnect->WithWill(will);
            options.WithConnectOptions(packetConnect);
            return AWS_OP_SUCCESS;
        });

    if (publisherContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> publisherClient = publisherContext.client;
    ASSERT_TRUE(publisherClient);

    ASSERT_TRUE(publisherClient->Start());
    publisherContext.connectionPromise.get_future().get();

    ASSERT_TRUE(subscriberClient->Start());
    subscriberContext.connectionPromise.get_future().get();

    /* Subscribe to test topic */
    Mqtt5::Subscription subscription(TEST_TOPIC, Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE, allocator);
    std::shared_ptr<Mqtt5::SubscribePacket> subscribe =
        Aws::Crt::MakeShared<Mqtt5::SubscribePacket>(allocator, allocator);

    subscribe->WithSubscription(std::move(subscription));

    std::promise<void> subscribed;
    ASSERT_TRUE(subscriberClient->Subscribe(
        subscribe, [&subscribed](int, std::shared_ptr<Mqtt5::SubAckPacket>) { subscribed.set_value(); }));
    subscribed.get_future().get();

    std::shared_ptr<Mqtt5::DisconnectPacket> disconnect =
        Aws::Crt::MakeShared<Mqtt5::DisconnectPacket>(allocator, allocator);
    disconnect->WithReasonCode(AWS_MQTT5_DRC_DISCONNECT_WITH_WILL_MESSAGE);
    ASSERT_TRUE(publisherClient->Stop(disconnect));
    publisherContext.stoppedPromise.get_future().get();

    {
        std::unique_lock<std::mutex> lock(receivedLock);
        receivedSignal.wait(lock, [&receivedWill]() -> bool { return receivedWill; });
    }

    ASSERT_TRUE(subscriberClient->Stop());
    subscriberContext.stoppedPromise.get_future().get();

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5WillTest, s_TestMqtt5WillTest)

//////////////////////////////////////////////////////////
// Error Operation Tests [ErrorOp-UC]
//////////////////////////////////////////////////////////

/*
 * [ErrorOp-UC1] Null Publish Test
 */
static int s_TestMqtt5NullPublish(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);
    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());

    // Invalid publish packet with empty topic
    ByteBuf payload = Aws::Crt::ByteBufFromCString("Mqtt5 Null Publish Test");
    std::shared_ptr<Mqtt5::PublishPacket> publish = Aws::Crt::MakeShared<Mqtt5::PublishPacket>(
        allocator, "", ByteCursorFromByteBuf(payload), Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE, allocator);

    /* Fail to publish because the topic is bad */
    ASSERT_FALSE(mqtt5Client->Publish(publish));

    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5NullPublish, s_TestMqtt5NullPublish)

/*
 * [ErrorOp-UC2] Null Subscribe Test
 */
static int s_TestMqtt5NullSubscribe(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);
    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());

    /* Subscribe to empty subscribe packet*/
    Vector<Mqtt5::Subscription> subscriptionList;
    subscriptionList.clear();
    std::shared_ptr<Mqtt5::SubscribePacket> subscribe =
        Aws::Crt::MakeShared<Mqtt5::SubscribePacket>(allocator, allocator);
    subscribe->WithSubscriptions(subscriptionList);
    ASSERT_FALSE(mqtt5Client->Subscribe(subscribe));

    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5NullSubscribe, s_TestMqtt5NullSubscribe)

/*
 * [ErrorOp-UC3] Null unsubscribe test
 */
static int s_TestMqtt5NullUnsubscribe(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);
    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());

    /* Subscribe to empty subscribe packet*/
    Vector<String> unsubList;
    unsubList.clear();
    std::shared_ptr<Mqtt5::UnsubscribePacket> unsubscribe =
        Aws::Crt::MakeShared<Mqtt5::UnsubscribePacket>(allocator, allocator);
    unsubscribe->WithTopicFilters(unsubList);
    ASSERT_FALSE(mqtt5Client->Unsubscribe(unsubscribe));

    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5NullUnsubscribe, s_TestMqtt5NullUnsubscribe)

/*
 * Reuse unsubscribe packet test.
 * The scenario in this test once caused memory leak, so the test ensures the issue is fixed for good.
 */
static int s_TestMqtt5ReuseUnsubscribePacket(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    const String TEST_TOPIC = "test/s_TestMqtt5NullUnsubscribe" + Aws::Crt::UUID().ToString();

    Mqtt5::Mqtt5ClientOptions mqtt5Options(allocator);
    mqtt5Options.WithHostName("www.example.com").WithPort(1111);
    std::shared_ptr<Mqtt5::Mqtt5Client> mqtt5Client = Mqtt5::Mqtt5Client::NewMqtt5Client(mqtt5Options, allocator);
    ASSERT_TRUE(mqtt5Client);

    Vector<String> unsubList{TEST_TOPIC};
    std::shared_ptr<Mqtt5::UnsubscribePacket> unsubscribe =
        Aws::Crt::MakeShared<Mqtt5::UnsubscribePacket>(allocator, allocator);
    unsubscribe->WithTopicFilters(unsubList);
    ASSERT_TRUE(mqtt5Client->Unsubscribe(unsubscribe));
    /* Unsubscribe once again using the same UnsubscribePacket. */
    ASSERT_TRUE(mqtt5Client->Unsubscribe(unsubscribe));

    ASSERT_TRUE(mqtt5Client->Stop());

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5ReuseUnsubscribePacket, s_TestMqtt5ReuseUnsubscribePacket)

//////////////////////////////////////////////////////////
// QoS1 Test Cases [QoS1-UC]
//////////////////////////////////////////////////////////

/*
 * [QoS1-UC1] Happy path. No drop in connection, no retry, no reconnect.
 */
static int s_TestMqtt5QoS1SubPub(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    const int MESSAGE_NUMBER = 10;
    const String TEST_TOPIC = "test/s_TestMqtt5QoS1SubPub" + Aws::Crt::UUID().ToString();
    std::vector<std::promise<void>> receivedMessages;
    for (int i = 0; i < MESSAGE_NUMBER; i++)
    {
        receivedMessages.push_back({});
    }

    Mqtt5TestContext subscriberContext = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT_IOT_CORE,
        [&receivedMessages,
         &TEST_TOPIC](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &context)
        {
            options.WithPublishReceivedCallback(
                [&receivedMessages, &TEST_TOPIC](const PublishReceivedEventData &eventData)
                {
                    String topic = eventData.publishPacket->getTopic();
                    if (topic == TEST_TOPIC)
                    {
                        ByteCursor payload = eventData.publishPacket->getPayload();
                        String message_string = String((const char *)payload.ptr, payload.len);
                        int message_int = atoi(message_string.c_str());
                        receivedMessages[message_int].set_value();
                    }
                });

            return AWS_OP_SUCCESS;
        });
    if (subscriberContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> subscriberClient = subscriberContext.client;
    ASSERT_TRUE(subscriberClient);

    Mqtt5TestContext publisherContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (publisherContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> publisherClient = publisherContext.client;
    ASSERT_TRUE(publisherClient);

    ASSERT_TRUE(publisherClient->Start());
    ASSERT_TRUE(publisherContext.connectionPromise.get_future().get());

    ASSERT_TRUE(subscriberClient->Start());
    ASSERT_TRUE(subscriberContext.connectionPromise.get_future().get());

    /* Subscribe to test topic */
    Mqtt5::Subscription subscription(TEST_TOPIC, Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE, allocator);
    std::shared_ptr<Mqtt5::SubscribePacket> subscribe = Aws::Crt::MakeShared<Mqtt5::SubscribePacket>(allocator);
    subscribe->WithSubscription(std::move(subscription));

    std::promise<void> subscribed;
    ASSERT_TRUE(subscriberClient->Subscribe(
        subscribe, [&subscribed](int, std::shared_ptr<Mqtt5::SubAckPacket>) { subscribed.set_value(); }));
    subscribed.get_future().get();

    /* Publish 10 messages to test topic */
    for (int i = 0; i < MESSAGE_NUMBER; i++)
    {
        std::string payload = std::to_string(i);
        std::shared_ptr<Mqtt5::PublishPacket> publish = Aws::Crt::MakeShared<Mqtt5::PublishPacket>(
            allocator,
            TEST_TOPIC,
            ByteCursorFromCString(payload.c_str()),
            Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE,
            allocator);
        ASSERT_TRUE(publisherClient->Publish(publish));
    }

    for (int i = 0; i < MESSAGE_NUMBER; i++)
    {
        receivedMessages[i].get_future().get();
    }

    ASSERT_TRUE(subscriberClient->Stop());
    subscriberContext.stoppedPromise.get_future().get();
    ASSERT_TRUE(publisherClient->Stop());
    publisherContext.stoppedPromise.get_future().get();

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5QoS1SubPub, s_TestMqtt5QoS1SubPub)

///*
// *  TODO:
// * [QoS1-UC2] Happy path. No drop in connection, no retry, no reconnect.
// * [QoS1-UC3] Similar to QoS1-UC2 setup, but Subscriber will reconnect with clean session
// * [QoS1-UC4] Similar to QoS1-UC2 setup, but the Subscriber stays disconnected longer than the retain message timeout.
// */

/*
 * [Retain-UC1] Set-And-Clear Test
 */
static int s_TestMqtt5RetainSetAndClear(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    const Aws::Crt::String TEST_TOPIC = "test/s_TestMqtt5RetainSetAndClear" + Aws::Crt::UUID().ToString();
    const Aws::Crt::String RETAIN_MESSAGE = "This is a retained message";
    std::promise<void> receivedRetainedMessage;
    std::promise<void> retainCleared;

    Mqtt5TestContext testContext1 = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (testContext1.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client1 = testContext1.client;
    ASSERT_TRUE(mqtt5Client1);

    Mqtt5TestContext testContext2 = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT_IOT_CORE,
        [&receivedRetainedMessage,
         &TEST_TOPIC](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &context)
        {
            options.WithPublishReceivedCallback(
                [&receivedRetainedMessage, &TEST_TOPIC](const PublishReceivedEventData &eventData)
                {
                    String topic = eventData.publishPacket->getTopic();
                    if (topic == TEST_TOPIC)
                    {
                        receivedRetainedMessage.set_value();
                    }
                });

            return AWS_OP_SUCCESS;
        });
    if (testContext2.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client2 = testContext2.client;
    ASSERT_TRUE(mqtt5Client2);

    Mqtt5TestContext testContext3 = createTestContext(
        allocator,
        MQTT5CONNECT_DIRECT_IOT_CORE,
        [&TEST_TOPIC](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &context)
        {
            options.WithPublishReceivedCallback(
                [&TEST_TOPIC](const PublishReceivedEventData &eventData)
                {
                    String topic = eventData.publishPacket->getTopic();
                    if (topic == TEST_TOPIC)
                    {
                        AWS_FATAL_ASSERT(false);
                    }
                });

            return AWS_OP_SUCCESS;
        });
    if (testContext3.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client3 = testContext3.client;
    ASSERT_TRUE(mqtt5Client3);

    // 1. client1 start and publish a retained message
    ASSERT_TRUE(mqtt5Client1->Start());
    ASSERT_TRUE(testContext1.connectionPromise.get_future().get());
    std::shared_ptr<Mqtt5::PublishPacket> setRetainPacket =
        Aws::Crt::MakeShared<Mqtt5::PublishPacket>(allocator, allocator);
    setRetainPacket->WithTopic(TEST_TOPIC).WithPayload(ByteCursorFromString(RETAIN_MESSAGE)).WithRetain(true);
    ASSERT_TRUE(mqtt5Client1->Publish(setRetainPacket));

    // 2. connect to client 2
    ASSERT_TRUE(mqtt5Client2->Start());
    ASSERT_TRUE(testContext2.connectionPromise.get_future().get());
    // 3. client2 subscribe to retain topic
    Mqtt5::Subscription subscription(TEST_TOPIC, Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE, allocator);
    std::shared_ptr<Mqtt5::SubscribePacket> subscribe =
        Aws::Crt::MakeShared<Mqtt5::SubscribePacket>(allocator, allocator);
    subscribe->WithSubscription(std::move(subscription));
    ASSERT_TRUE(mqtt5Client2->Subscribe(subscribe));

    receivedRetainedMessage.get_future().get();

    // Stop client2
    ASSERT_TRUE(mqtt5Client2->Stop());
    testContext2.stoppedPromise.get_future().get();

    // 4. client1 reset retain message
    std::shared_ptr<Mqtt5::PublishPacket> clearRetainPacket =
        Aws::Crt::MakeShared<Mqtt5::PublishPacket>(allocator, allocator);
    clearRetainPacket->WithTopic(TEST_TOPIC).WithRetain(true);
    ASSERT_TRUE(mqtt5Client1->Publish(
        clearRetainPacket,
        [&retainCleared](int errorCode, std::shared_ptr<PublishResult> result)
        {
            if (errorCode == AWS_ERROR_SUCCESS)
            {
                retainCleared.set_value();
            }
        }));

    // 5. client3 start and subscribe to retain topic
    ASSERT_TRUE(mqtt5Client3->Start());
    ASSERT_TRUE(testContext3.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client3->Subscribe(subscribe));

    // Wait for client 3
    aws_thread_current_sleep(2000 * 1000 * 1000);

    ASSERT_TRUE(mqtt5Client3->Stop());
    testContext3.stoppedPromise.get_future().get();
    ASSERT_TRUE(mqtt5Client1->Stop());
    testContext1.stoppedPromise.get_future().get();

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5RetainSetAndClear, s_TestMqtt5RetainSetAndClear)

//////////////////////////////////////////////////////////
// Interruption tests [IT-UC]
//////////////////////////////////////////////////////////

/*
 * [IT-UC1] Interrupt subscription
 */
static int s_TestMqtt5InterruptSub(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());

    const Aws::Crt::String TEST_TOPIC = "test/s_TestMqtt5InterruptSub" + Aws::Crt::UUID().ToString();
    /* Subscribe to test topic */
    Mqtt5::Subscription subscription(TEST_TOPIC, Mqtt5::QOS::AWS_MQTT5_QOS_AT_MOST_ONCE, allocator);
    std::shared_ptr<Mqtt5::SubscribePacket> subscribe =
        Aws::Crt::MakeShared<Mqtt5::SubscribePacket>(allocator, allocator);
    subscribe->WithSubscription(std::move(subscription));
    ASSERT_TRUE(mqtt5Client->Subscribe(subscribe));

    /* Stop immediately */
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5InterruptSub, s_TestMqtt5InterruptSub)

/*
 * [IT-UC2] Interrupt Unsubscription
 */
static int s_TestMqtt5InterruptUnsub(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());

    const Aws::Crt::String TEST_TOPIC = "test/s_TestMqtt5InterruptUnsub" + Aws::Crt::UUID().ToString();

    /* Unsub from topic*/
    Vector<String> topics;
    topics.push_back(TEST_TOPIC);
    std::shared_ptr<Mqtt5::UnsubscribePacket> unsub =
        Aws::Crt::MakeShared<Mqtt5::UnsubscribePacket>(allocator, allocator);
    unsub->WithTopicFilters(topics);
    ASSERT_TRUE(mqtt5Client->Unsubscribe(unsub));

    /* Stop immediately */
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5InterruptUnsub, s_TestMqtt5InterruptUnsub)

/*
 * [IT-UC3] Interrupt Publish
 */
static int s_TestMqtt5InterruptPublishQoS1(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());

    const Aws::Crt::String TEST_TOPIC = "test/s_TestMqtt5InterruptPublish" + Aws::Crt::UUID().ToString();

    /* Publish QOS1 to test topic */
    ByteBuf payload = Aws::Crt::ByteBufFromCString("Hello World");
    std::shared_ptr<Mqtt5::PublishPacket> publish = Aws::Crt::MakeShared<Mqtt5::PublishPacket>(
        allocator, TEST_TOPIC, ByteCursorFromByteBuf(payload), Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE, allocator);
    ASSERT_TRUE(mqtt5Client->Publish(publish));

    /* Stop immediately */
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5InterruptPublishQoS1, s_TestMqtt5InterruptPublishQoS1)

//////////////////////////////////////////////////////////
// Misc Tests
//////////////////////////////////////////////////////////

/*
 * [Misc] test_operation_statistics_uc1
 */
static int s_TestMqtt5OperationStatisticsSimple(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    const String TEST_TOPIC = "test/MQTT5_Binding_CPP" + Aws::Crt::UUID().ToString();

    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());

    /* Make sure the operations are empty */
    Mqtt5::Mqtt5ClientOperationStatistics statistics = mqtt5Client->GetOperationStatistics();
    ASSERT_INT_EQUALS(0, statistics.incompleteOperationCount);
    ASSERT_INT_EQUALS(0, statistics.incompleteOperationSize);
    ASSERT_INT_EQUALS(0, statistics.unackedOperationCount);
    ASSERT_INT_EQUALS(0, statistics.unackedOperationSize);

    /* Publish message 1 to test topic */
    ByteBuf payload = Aws::Crt::ByteBufFromCString("Hello World");
    std::shared_ptr<Mqtt5::PublishPacket> publish = Aws::Crt::MakeShared<Mqtt5::PublishPacket>(
        allocator, TEST_TOPIC, ByteCursorFromByteBuf(payload), Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE, allocator);
    ASSERT_TRUE(mqtt5Client->Publish(publish));

    // Sleep and wait for message received
    aws_thread_current_sleep(2000 * 1000 * 1000);

    /* Make sure the operations are empty */
    statistics = mqtt5Client->GetOperationStatistics();
    ASSERT_INT_EQUALS(0, statistics.incompleteOperationCount);
    ASSERT_INT_EQUALS(0, statistics.incompleteOperationSize);
    ASSERT_INT_EQUALS(0, statistics.unackedOperationCount);
    ASSERT_INT_EQUALS(0, statistics.unackedOperationSize);

    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5OperationStatisticsSimple, s_TestMqtt5OperationStatisticsSimple)

/* Mqtt5-to-Mqtt3 Adapter Test */

/* Test Helper Functions */
/* Test connect and disconnect with MqttConnection Interface */
static int s_ConnectAndDisconnectThroughMqtt3(std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> connection)
{
    std::promise<bool> connectionCompletedPromise;
    std::promise<void> connectionClosedPromise;
    auto onConnectionCompleted =
        [&](Aws::Crt::Mqtt::MqttConnection &, int errorCode, Aws::Crt::Mqtt::ReturnCode returnCode, bool)
    {
        (void)returnCode;
        if (errorCode)
        {
            connectionCompletedPromise.set_value(false);
        }
        else
        {
            connectionCompletedPromise.set_value(true);
        }
    };
    auto onDisconnect = [&](Aws::Crt::Mqtt::MqttConnection &) { connectionClosedPromise.set_value(); };
    connection->OnConnectionCompleted = std::move(onConnectionCompleted);
    connection->OnDisconnect = std::move(onDisconnect);

    // Mqtt5 Test client policy only allows client id start with "test-"
    Aws::Crt::UUID Uuid;
    Aws::Crt::String uuidStr = "test-" + Uuid.ToString();

    if (!connection->Connect(uuidStr.c_str(), true /*cleanSession*/, 5000 /*keepAliveTimeSecs*/))
    {
        printf("Failed to connect");
        return AWS_OP_ERR;
    }
    if (connectionCompletedPromise.get_future().get() == false)
    {
        printf("Connection failed");
        return AWS_OP_ERR;
    }
    if (connection->Disconnect())
    {
        connectionClosedPromise.get_future().wait();
    }
    return AWS_OP_SUCCESS;
}

/* Test connect and disconnect with Mqtt5 Interface */
static int s_AdapterConenctAndDisconnectThroughMqtt5(
    Mqtt5::Mqtt5ClientOptions &mqtt5Options,
    Aws::Crt::Allocator *allocator,
    const char *clientName = "Client")
{

    std::promise<bool> connectionPromise;
    std::promise<void> stoppedPromise;
    s_setupConnectionLifeCycle(mqtt5Options, connectionPromise, stoppedPromise, clientName);

    std::shared_ptr<Mqtt5::Mqtt5Client> mqtt5Client = Mqtt5::Mqtt5Client::NewMqtt5Client(mqtt5Options, allocator);
    ASSERT_TRUE(mqtt5Client);
    std::shared_ptr<Mqtt::MqttConnection> mqttConnection =
        Mqtt::MqttConnection::NewConnectionFromMqtt5Client(mqtt5Client);
    ASSERT_TRUE(mqttConnection && *mqttConnection);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    stoppedPromise.get_future().get();

    return AWS_OP_SUCCESS;
}

/*
 * [Mqtt5to3Adapter-UC1] Happy path. Minimal creation and cleanup
 */
static int s_TestMqtt5to3AdapterNewConnectionMin(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Mqtt5::Mqtt5ClientOptions mqtt5Options(allocator);
    // Hardcoded the host name and port for creation test
    mqtt5Options.WithHostName("localhost").WithPort(1883);
    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(3000);
    std::shared_ptr<Mqtt5::Mqtt5Client> mqtt5Client = Mqtt5::Mqtt5Client::NewMqtt5Client(mqtt5Options, allocator);
    ASSERT_TRUE(mqtt5Client);
    std::shared_ptr<Mqtt::MqttConnection> mqttConnection =
        Mqtt::MqttConnection::NewConnectionFromMqtt5Client(mqtt5Client);
    ASSERT_TRUE(mqttConnection);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5to3AdapterNewConnectionMin, s_TestMqtt5to3AdapterNewConnectionMin)

/*
 * [Mqtt5to3Adapter-UC2] Maximum creation and cleanup
 */
static int s_TestMqtt5to3AdapterNewClientFull(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Mqtt5::Mqtt5ClientOptions mqtt5Options(allocator);
    // Hardcoded the host name and port for creation test
    mqtt5Options.WithHostName("localhost").WithPort(1883);

    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(3000);

    // Setup will
    const Aws::Crt::String TEST_TOPIC = "test/MQTT5_Binding_CPP/s_TestMqtt5NewClientFull";
    ByteBuf will_payload = Aws::Crt::ByteBufFromCString("Will Test");
    std::shared_ptr<Mqtt5::PublishPacket> will = Aws::Crt::MakeShared<Mqtt5::PublishPacket>(
        allocator, TEST_TOPIC, ByteCursorFromByteBuf(will_payload), Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE, allocator);

    std::shared_ptr<Aws::Crt::Mqtt5::ConnectPacket> packetConnect =
        Aws::Crt::MakeShared<Aws::Crt::Mqtt5::ConnectPacket>(allocator);
    packetConnect->WithClientId("s_TestMqtt5DirectConnectionFull" + Aws::Crt::UUID().ToString())
        .WithKeepAliveIntervalSec(1000)
        .WithMaximumPacketSizeBytes(1000L)
        .WithReceiveMaximum(1000)
        .WithRequestProblemInformation(true)
        .WithRequestResponseInformation(true)
        .WithSessionExpiryIntervalSec(1000L)
        .WithWill(will)
        .WithWillDelayIntervalSec(1000);

    Aws::Crt::Mqtt5::UserProperty userProperty("PropertyName", "PropertyValue");
    packetConnect->WithUserProperty(std::move(userProperty));

    Aws::Crt::Mqtt5::ReconnectOptions reconnectOptions = {
        Mqtt5::JitterMode::AWS_EXPONENTIAL_BACKOFF_JITTER_FULL, 1000, 1000, 1000};

    mqtt5Options.WithConnectOptions(packetConnect);
    mqtt5Options.WithBootstrap(ApiHandle::GetOrCreateStaticDefaultClientBootstrap());
    mqtt5Options.WithSocketOptions(socketOptions);
    mqtt5Options.WithSessionBehavior(Mqtt5::ClientSessionBehaviorType::AWS_MQTT5_CSBT_REJOIN_POST_SUCCESS);
    mqtt5Options.WithClientExtendedValidationAndFlowControl(
        Mqtt5::ClientExtendedValidationAndFlowControl::AWS_MQTT5_EVAFCO_NONE);
    mqtt5Options.WithOfflineQueueBehavior(
        Mqtt5::ClientOperationQueueBehaviorType::AWS_MQTT5_COQBT_FAIL_QOS0_PUBLISH_ON_DISCONNECT);
    mqtt5Options.WithReconnectOptions(reconnectOptions);
    mqtt5Options.WithPingTimeoutMs(10000);
    mqtt5Options.WithConnackTimeoutMs(10000);
    mqtt5Options.WithAckTimeoutSec(60000);

    std::promise<bool> connectionPromise;
    std::promise<void> stoppedPromise;

    s_setupConnectionLifeCycle(mqtt5Options, connectionPromise, stoppedPromise);

    std::shared_ptr<Mqtt5::Mqtt5Client> mqtt5Client = Mqtt5::Mqtt5Client::NewMqtt5Client(mqtt5Options, allocator);
    ASSERT_TRUE(mqtt5Client);
    std::shared_ptr<Mqtt::MqttConnection> mqttConnection =
        Mqtt::MqttConnection::NewConnectionFromMqtt5Client(mqtt5Client);
    ASSERT_TRUE(mqttConnection && *mqttConnection);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5to3AdapterNewClientFull, s_TestMqtt5to3AdapterNewClientFull)

/*
 * [Mqtt5to3Adapter-UC3] Happy path. Minimal direct connection through Mqtt3 Interface
 */
static int s_TestMqtt5to3AdapterDirectConnectionMinimalThroughMqtt3(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    std::shared_ptr<Mqtt::MqttConnection> mqttConnection =
        Mqtt::MqttConnection::NewConnectionFromMqtt5Client(mqtt5Client);
    ASSERT_TRUE(mqttConnection);
    int connectResult = s_ConnectAndDisconnectThroughMqtt3(mqttConnection);
    ASSERT_SUCCESS(connectResult);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(
    Mqtt5to3AdapterDirectConnectionMinimalThroughMqtt3,
    s_TestMqtt5to3AdapterDirectConnectionMinimalThroughMqtt3)

/*
 * [Mqtt5to3Adapter-UC4] Websocket creation and connection through Mqtt3 Interface
 */
static int s_TestMqtt5to3AdapterWSConnectionMinimalThroughMqtt3(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Mqtt5TestContext testContext = createTestContext(
        allocator,
        MQTT5CONNECT_WS_IOT_CORE,
        [](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &context)
        {
            options.WithWebsocketHandshakeTransformCallback(
                [](std::shared_ptr<Http::HttpRequest>, const OnWebSocketHandshakeInterceptComplete &)
                { AWS_FATAL_ASSERT(false); });

            return AWS_OP_SUCCESS;
        });

    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    Aws::Crt::Auth::CredentialsProviderChainDefaultConfig defaultConfig;
    std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider =
        Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(defaultConfig);

    ASSERT_TRUE(provider);

    Aws::Iot::WebsocketConfig config("us-east-1", provider);

    std::promise<void> mqtt311Signing;

    std::shared_ptr<Mqtt::MqttConnection> mqttConnection =
        Mqtt::MqttConnection::NewConnectionFromMqtt5Client(mqtt5Client);
    ASSERT_TRUE(mqttConnection);

    mqttConnection->WebsocketInterceptor = [&config, &mqtt311Signing](
                                               std::shared_ptr<Aws::Crt::Http::HttpRequest> req,
                                               const Aws::Crt::Mqtt::OnWebSocketHandshakeInterceptComplete &onComplete)
    {
        auto signingComplete = [onComplete](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &req1, int errorCode)
        { onComplete(req1, errorCode); };

        auto signerConfig = config.CreateSigningConfigCb();

        config.Signer->SignRequest(req, *signerConfig, signingComplete);
        mqtt311Signing.set_value();
    };

    int connectResult = s_ConnectAndDisconnectThroughMqtt3(mqttConnection);
    ASSERT_SUCCESS(connectResult);

    mqtt311Signing.get_future().get();

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5to3AdapterWSConnectionMinimalThroughMqtt3, s_TestMqtt5to3AdapterWSConnectionMinimalThroughMqtt3)

/*
 * [Mqtt5to3Adapter-UC5] IoT MutalTLS creation and cleanup with Mqtt5ClientBuilder through Mqtt3 Interface
 */
static int s_TestMqtt5to3AdapterWithIoTConnectionThroughMqtt3(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    // Created a Mqtt311 Connection from mqtt5Client. The options are setup by the builder.
    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> mqttConnection =
        Mqtt::MqttConnection::NewConnectionFromMqtt5Client(mqtt5Client);
    ASSERT_TRUE(mqttConnection);
    int connectResult = s_ConnectAndDisconnectThroughMqtt3(mqttConnection);
    ASSERT_SUCCESS(connectResult);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5to3AdapterWithIoTConnectionThroughMqtt3, s_TestMqtt5to3AdapterWithIoTConnectionThroughMqtt3)

/*
 * [Mqtt5to3Adapter-UC6] MutalTLS connection through Mqtt3 Interface
 */
static int s_TestMqtt5to3AdapterDirectConnectionWithMutualTLSThroughMqtt3(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    // Created a Mqtt311 Connection from mqtt5Client. The options are setup by the builder.
    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> mqttConnection =
        Mqtt::MqttConnection::NewConnectionFromMqtt5Client(mqtt5Client);
    ASSERT_TRUE(mqttConnection);
    int connectResult = s_ConnectAndDisconnectThroughMqtt3(mqttConnection);
    ASSERT_SUCCESS(connectResult);

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(
    Mqtt5to3AdapterDirectConnectionWithMutualTLSThroughMqtt3,
    s_TestMqtt5to3AdapterDirectConnectionWithMutualTLSThroughMqtt3)

/*
 * [Mqtt5to3Adapter-UC7] Happy path. Minimal direct connection through Mqtt5 Interface
 */
static int s_TestMqtt5to3AdapterDirectConnectionMinimalThroughMqtt5(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Mqtt5TestEnvVars mqtt5TestVars(allocator, MQTT5CONNECT_DIRECT);
    if (!mqtt5TestVars)
    {
        printf("Environment Variables are not set for the test, skip the test");
        return AWS_OP_SKIP;
    }

    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(3000);
    Mqtt5::Mqtt5ClientOptions mqtt5Options(allocator);

    mqtt5Options.WithHostName(mqtt5TestVars.m_hostname_string);
    mqtt5Options.WithPort(mqtt5TestVars.m_port_value);

    return s_AdapterConenctAndDisconnectThroughMqtt5(mqtt5Options, allocator);
}
AWS_TEST_CASE(
    Mqtt5to3AdapterDirectConnectionMinimalThroughMqtt5,
    s_TestMqtt5to3AdapterDirectConnectionMinimalThroughMqtt5)

/*
 * [Mqtt5to3Adapter-UC8] Websocket creation and connection through Mqtt5 Interface
 */
static int s_TestMqtt5to3AdapterWSConnectionMinimalThroughMqtt5(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Mqtt5TestContext testContext = createTestContext(
        allocator,
        MQTT5CONNECT_WS,
        [](Mqtt5ClientOptions &options, const Mqtt5TestEnvVars &, Mqtt5TestContext &context)
        {
            options.WithWebsocketHandshakeTransformCallback(
                [](std::shared_ptr<Http::HttpRequest>, const OnWebSocketHandshakeInterceptComplete &)
                { AWS_FATAL_ASSERT(false); });

            return AWS_OP_SUCCESS;
        });

    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    std::promise<void> mqtt311Signed;

    std::shared_ptr<Mqtt::MqttConnection> mqttConnection =
        Mqtt::MqttConnection::NewConnectionFromMqtt5Client(mqtt5Client);
    ASSERT_TRUE(mqttConnection);

    mqttConnection->WebsocketInterceptor = [&mqtt311Signed](
                                               std::shared_ptr<Aws::Crt::Http::HttpRequest> req,
                                               const Aws::Crt::Mqtt::OnWebSocketHandshakeInterceptComplete &onComplete)
    {
        onComplete(req, AWS_ERROR_SUCCESS);
        mqtt311Signed.set_value();
    };

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();

    mqtt311Signed.get_future().get();

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5to3AdapterWSConnectionMinimalThroughMqtt5, s_TestMqtt5to3AdapterWSConnectionMinimalThroughMqtt5)

/*
 * [Mqtt5to3Adapter-UC9] IoT MutalTLS creation and cleanup with Mqtt5ClientBuilder through Mqtt5 Interface
 */
static int s_TestMqtt5to3AdapterWithIoTConnectionThroughMqtt5(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    // Created a Mqtt311 Connection from mqtt5Client. The options are setup by the builder.
    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> mqttConnection =
        Mqtt::MqttConnection::NewConnectionFromMqtt5Client(mqtt5Client);
    ASSERT_TRUE(mqttConnection);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());

    /* Stop immediately */
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5to3AdapterWithIoTConnectionThroughMqtt5, s_TestMqtt5to3AdapterWithIoTConnectionThroughMqtt5)

/*
 * [Mqtt5to3Adapter-UC10] MutalTLS connection through Mqtt5 Interface
 */
static int s_TestMqtt5to3AdapterDirectConnectionWithMutualTLSThroughMqtt5(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    Mqtt5TestEnvVars mqtt5TestVars(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (!mqtt5TestVars)
    {
        printf("Environment Variables are not set for the test, skip the test");
        return AWS_OP_SKIP;
    }

    Mqtt5::Mqtt5ClientOptions mqtt5Options(allocator);
    mqtt5Options.WithHostName(mqtt5TestVars.m_hostname_string);
    mqtt5Options.WithPort(443);

    Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitClientWithMtls(
        mqtt5TestVars.m_certificate_path_string.c_str(), mqtt5TestVars.m_private_key_path_string.c_str(), allocator);

    Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    ASSERT_TRUE(tlsContext);
    Aws::Crt::Io::TlsConnectionOptions tlsConnection = tlsContext.NewConnectionOptions();
    ASSERT_TRUE(tlsConnection);
    ASSERT_TRUE(tlsConnection.SetAlpnList("x-amzn-mqtt-ca"));
    mqtt5Options.WithTlsConnectionOptions(tlsConnection);

    return s_AdapterConenctAndDisconnectThroughMqtt5(mqtt5Options, allocator);
}
AWS_TEST_CASE(
    Mqtt5to3AdapterDirectConnectionWithMutualTLSThroughMqtt5,
    s_TestMqtt5to3AdapterDirectConnectionWithMutualTLSThroughMqtt5)

/*
 * [Mqtt5to3Adapter-UC11] Test sub/unsub/publish operations through adapter
 */
static int s_TestMqtt5to3AdapterOperations(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    String testUUID = Aws::Crt::UUID().ToString();
    String testTopic = "test/MQTT5to3Adapter_" + testUUID;
    ByteBuf testPayload = Aws::Crt::ByteBufFromCString("PUBLISH ME!");

    std::promise<void> subscribed;
    std::promise<void> published;
    std::promise<void> unsubscribed;

    uint8_t received = 0;
    std::mutex mutex;
    std::condition_variable cv;

    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    // Created a Mqtt311 Connection from mqtt5Client. The options are setup by the builder.
    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> mqttConnection =
        Mqtt::MqttConnection::NewConnectionFromMqtt5Client(mqtt5Client);
    ASSERT_TRUE(mqttConnection);

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());

    auto onMessage = [&](Mqtt::MqttConnection &, const String &topic, const ByteBuf &payload, bool, Mqtt::QOS, bool)
    {
        std::lock_guard<std::mutex> lock(mutex);
        ++received;
        cv.notify_one();
    };
    auto onSubAck = [&](Mqtt::MqttConnection &, uint16_t packetId, const Aws::Crt::String &topic, Mqtt::QOS qos, int)
    { subscribed.set_value(); };
    auto onPubAck = [&](Mqtt::MqttConnection &, uint16_t packetId, int) { published.set_value(); };
    auto onUnsubAck = [&](Mqtt::MqttConnection &, uint16_t packetId, int) { unsubscribed.set_value(); };

    mqttConnection->Subscribe(
        testTopic.c_str(), Mqtt::QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, std::move(onMessage), std::move(onSubAck));
    subscribed.get_future().get();

    mqttConnection->Publish(testTopic.c_str(), Mqtt::QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, false, testPayload, onPubAck);
    published.get_future().get();

    // Wait for message received
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&]() { return received > 0; });
    }

    mqttConnection->Unsubscribe(testTopic.c_str(), onUnsubAck);
    unsubscribed.get_future().get();

    published = {};
    mqttConnection->Publish(testTopic.c_str(), Mqtt::QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, false, testPayload, onPubAck);

    // wait for publish
    published.get_future().get();

    // give a chance for the publish to reflect if we were subscribed (which we're not)
    aws_thread_current_sleep(2000 * 1000 * 1000);

    /* Stop immediately */
    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();

    // no second publish
    {
        std::lock_guard<std::mutex> lock(mutex);
        ASSERT_TRUE(received == 1);
    }

    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5to3AdapterOperations, s_TestMqtt5to3AdapterOperations)

/*
 * [Mqtt5to3Adapter-UC11] Test s_TestMqtt5to3AdapterNullPubAck
 * The unit test would have memory leak if the callback data for incomplete publish was not released.
 */
static int s_TestMqtt5to3AdapterNullPubAck(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);
    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    String testUUID = Aws::Crt::UUID().ToString();
    String testTopic = "test/MQTT5to3Adapter_" + testUUID;
    ByteBuf testPayload = Aws::Crt::ByteBufFromCString("PUBLISH ME!");

    // Created a Mqtt311 Connection from mqtt5Client. The options are setup by the builder.
    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> mqttConnection =
        Mqtt::MqttConnection::NewConnectionFromMqtt5Client(mqtt5Client);
    ASSERT_TRUE(mqttConnection);

    // Publish an offline message to create an incomplete publish operation
    mqttConnection->Publish(testTopic.c_str(), Mqtt::QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, false, testPayload, NULL);

    // If the incomplete operation callback was not called, there would be a memory leak as the callback data was not
    // released
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(Mqtt5to3AdapterNullPubAck, s_TestMqtt5to3AdapterNullPubAck)

/*
 * [Mqtt5to3Adapter-UC11] Test one mqtt5 client with multiple adapters
 */
static int s_TestMqtt5to3AdapterMultipleAdapters(Aws::Crt::Allocator *allocator, void *)
{
    ApiHandle apiHandle(allocator);

    String randomID = Aws::Crt::UUID().ToString();
    String testTopic1 = "test/topic1_" + randomID;
    String testTopic2 = "test/topic2_" + randomID;

    Mqtt5TestContext testContext = createTestContext(allocator, MQTT5CONNECT_DIRECT_IOT_CORE);
    if (testContext.testDirective == AWS_OP_SKIP)
    {
        return AWS_OP_SKIP;
    }

    std::shared_ptr<Mqtt5Client> mqtt5Client = testContext.client;
    ASSERT_TRUE(mqtt5Client);

    // Created a Mqtt311 Connection from mqtt5Client. The options are setup by the builder.
    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> mqttConnection1 =
        Mqtt::MqttConnection::NewConnectionFromMqtt5Client(mqtt5Client);
    ASSERT_TRUE(mqttConnection1);

    // Created a Mqtt311 Connection from mqtt5Client. The options are setup by the builder.
    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> mqttConnection2 =
        Mqtt::MqttConnection::NewConnectionFromMqtt5Client(mqtt5Client);
    ASSERT_TRUE(mqttConnection2);

    size_t received1 = 0;
    size_t received2 = 0;
    std::mutex mutex;
    std::condition_variable cv;
    std::promise<void> subscribed1;
    std::promise<void> subscribed2;
    std::promise<void> published;
    ByteBuf testPayload = Aws::Crt::ByteBufFromCString("PUBLISH ME!");

    auto onMessage1 = [&](Mqtt::MqttConnection &, const String &topic, const ByteBuf &payload, bool, Mqtt::QOS, bool)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++received1;
            cv.notify_one();
        }
    };

    auto onSubAck1 = [&](Mqtt::MqttConnection &, uint16_t packetId, const Aws::Crt::String &topic, Mqtt::QOS qos, int)
    { subscribed1.set_value(); };

    auto onMessage2 = [&](Mqtt::MqttConnection &, const String &topic, const ByteBuf &payload, bool, Mqtt::QOS, bool)
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++received2;
            cv.notify_one();
        }
    };

    auto onSubAck2 = [&](Mqtt::MqttConnection &, uint16_t packetId, const Aws::Crt::String &topic, Mqtt::QOS qos, int)
    { subscribed2.set_value(); };

    ASSERT_TRUE(mqtt5Client->Start());
    ASSERT_TRUE(testContext.connectionPromise.get_future().get());

    mqttConnection1->Subscribe(testTopic1.c_str(), Mqtt::QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, onMessage1, onSubAck1);
    subscribed1.get_future().get();

    mqttConnection2->Subscribe(testTopic2.c_str(), Mqtt::QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, onMessage2, onSubAck2);
    subscribed2.get_future().get();

    auto onPubAck = [&](Mqtt::MqttConnection &, uint16_t packetId, int) { published.set_value(); };

    // Publish to testTopic1
    mqttConnection1->Publish(testTopic1.c_str(), Mqtt::QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, false, testPayload, onPubAck);
    // wait for publish completion
    published.get_future().get();

    published = {};
    // Publish to testTopic2
    mqttConnection1->Publish(testTopic2.c_str(), Mqtt::QOS::AWS_MQTT_QOS_AT_LEAST_ONCE, false, testPayload, onPubAck);
    // wait for publish
    published.get_future().get();

    // wait for message received
    {
        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&received1, &received2]() { return received1 > 0 && received2 > 0; });
    }

    ASSERT_TRUE(mqtt5Client->Stop());
    testContext.stoppedPromise.get_future().get();

    {
        std::lock_guard<std::mutex> lock(mutex);
        ASSERT_TRUE(received1 == 1);
        ASSERT_TRUE(received2 == 1);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(Mqtt5to3AdapterMultipleAdapters, s_TestMqtt5to3AdapterMultipleAdapters)

#endif // !BYO_CRYPTO∏
