/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>

#include <aws/common/common.h>
#include <aws/common/environment.h>
#include <aws/common/string.h>
#include <aws/crt/UUID.h>
#include <aws/iot/MqttClient.h>
#include <aws/iot/MqttCommon.h>

#include <utility>

#include <aws/testing/aws_test_harness.h>

AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_direct_hostname, "AWS_TEST_MQTT311_DIRECT_MQTT_HOST");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_direct_port, "AWS_TEST_MQTT311_DIRECT_MQTT_PORT");

AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt311_test_envName_direct_basicauth_hostname,
    "AWS_TEST_MQTT311_DIRECT_MQTT_BASIC_AUTH_HOST");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt311_test_envName_direct_basicauth_port,
    "AWS_TEST_MQTT311_DIRECT_MQTT_BASIC_AUTH_PORT");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_basicauth_username, "AWS_TEST_MQTT311_BASIC_AUTH_USERNAME");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_basicauth_password, "AWS_TEST_MQTT311_BASIC_AUTH_PASSWORD");

AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_direct_tls_hostname, "AWS_TEST_MQTT311_DIRECT_MQTT_TLS_HOST");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_direct_tls_port, "AWS_TEST_MQTT311_DIRECT_MQTT_TLS_PORT");

AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_iot_hostname, "AWS_TEST_MQTT311_IOT_CORE_HOST");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_iot_cert, "AWS_TEST_MQTT311_IOT_CORE_RSA_CERT");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_iot_key, "AWS_TEST_MQTT311_IOT_CORE_RSA_KEY");

AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_proxy_hostname, "AWS_TEST_MQTT311_PROXY_HOST");
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_proxy_port, "AWS_TEST_MQTT311_PROXY_PORT");

/*
self._setenv(env, "AWS_TEST_MQTT311_IOT_CORE_REGION", "us-east-1")

self._setenv_secret_file(env, "AWS_TEST_MQTT311_CERTIFICATE_FILE", "ci/mqtt5/us/Mqtt5Prod/cert")
self._setenv_secret_file(env, "AWS_TEST_MQTT311_KEY_FILE", "ci/mqtt5/us/Mqtt5Prod/key")
self._setenv_secret(env, "AWS_TEST_MQTT311_WS_MQTT_HOST", "ci/mqtt5/us/mosquitto/host")
self._setenv(env, "AWS_TEST_MQTT311_WS_MQTT_PORT", "8080")
self._setenv_secret(env, "AWS_TEST_MQTT311_WS_MQTT_BASIC_AUTH_HOST", "ci/mqtt5/us/mosquitto/host")
self._setenv(env, "AWS_TEST_MQTT311_WS_MQTT_BASIC_AUTH_PORT", "8090")
# self._setenv_secret(env, "AWS_TEST_MQTT311_WS_MQTT_TLS_HOST", "ci/mqtt5/us/mosquitto/host")
self._setenv(env, "AWS_TEST_MQTT311_WS_MQTT_TLS_PORT", "8081")
*/

#if !BYO_CRYPTO
static int s_TestMqttClientResourceSafety(Aws::Crt::Allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        Aws::Crt::ApiHandle apiHandle(allocator);
        Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();

        Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
        ASSERT_TRUE(tlsContext);

        Aws::Crt::Io::SocketOptions socketOptions;
        socketOptions.SetConnectTimeoutMs(3000);

        Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
        ASSERT_TRUE(eventLoopGroup);

        Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
        ASSERT_TRUE(defaultHostResolver);

        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
        ASSERT_TRUE(allocator);
        clientBootstrap.EnableBlockingShutdown();

        Aws::Crt::Mqtt::MqttClient mqttClient(clientBootstrap, allocator);
        ASSERT_TRUE(mqttClient);

        Aws::Crt::Mqtt::MqttClient mqttClientMoved = std::move(mqttClient);
        ASSERT_TRUE(mqttClientMoved);

        auto mqttConnection = mqttClientMoved.NewConnection("www.example.com", 443, socketOptions, tlsContext);

        mqttConnection->SetOnMessageHandler(
            [](Aws::Crt::Mqtt::MqttConnection &, const Aws::Crt::String &, const Aws::Crt::ByteBuf &) {});
        mqttConnection->Disconnect();
        ASSERT_TRUE(mqttConnection);

        // NOLINTNEXTLINE
        ASSERT_FALSE(mqttClient);
    }

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(MqttClientResourceSafety, s_TestMqttClientResourceSafety)

static int s_TestMqttClientNewConnectionUninitializedTlsContext(Aws::Crt::Allocator *allocator, void *ctx)
{
    (void)ctx;
    {
        Aws::Crt::ApiHandle apiHandle(allocator);

        Aws::Crt::Io::SocketOptions socketOptions;
        socketOptions.SetConnectTimeoutMs(3000);

        Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
        ASSERT_TRUE(eventLoopGroup);

        Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
        ASSERT_TRUE(defaultHostResolver);

        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
        ASSERT_TRUE(allocator);
        clientBootstrap.EnableBlockingShutdown();

        Aws::Crt::Mqtt::MqttClient mqttClient(clientBootstrap, allocator);
        ASSERT_TRUE(mqttClient);

        // Intentionally use a TlsContext that hasn't been initialized.
        Aws::Crt::Io::TlsContext tlsContext;

        // Passing the uninitialized TlsContext should result in a null connection, not one in an undefined state
        auto mqttConnection = mqttClient.NewConnection("www.example.com", 443, socketOptions, tlsContext);

        ASSERT_TRUE(mqttConnection == nullptr);
        ASSERT_TRUE(aws_last_error() == AWS_ERROR_INVALID_ARGUMENT);
    }

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE(MqttClientNewConnectionUninitializedTlsContext, s_TestMqttClientNewConnectionUninitializedTlsContext)
#endif // !BYO_CRYPTO

static int s_ConnectAndDisconnect(std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> connection)
{
    std::promise<bool> connectionCompletedPromise;
    std::promise<void> connectionClosedPromise;
    auto onConnectionCompleted =
        [&](Aws::Crt::Mqtt::MqttConnection &, int errorCode, Aws::Crt::Mqtt::ReturnCode returnCode, bool) {
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

    Aws::Crt::UUID Uuid;
    Aws::Crt::String uuidStr = Uuid.ToString();

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

/*
 * [ConnDC-UC1] Happy path. Direct connection with minimal configuration
 */
static int s_TestMqtt311DirectConnectionMinimal(Aws::Crt::Allocator *allocator, void *)
{
    struct aws_string *endpoint = NULL;
    struct aws_string *port = NULL;

    int error = aws_get_environment_value(allocator, s_mqtt311_test_envName_direct_hostname, &endpoint);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_direct_port, &port);

    bool isEveryEnvVarSet = (endpoint && port);
    if (isEveryEnvVarSet == true)
    {
        isEveryEnvVarSet = (aws_string_is_valid(endpoint) && aws_string_is_valid(port));
    }
    if (error != AWS_OP_SUCCESS || isEveryEnvVarSet == false)
    {
        printf("Environment Variables are not set for the test, skip the test");
        aws_string_destroy(endpoint);
        aws_string_destroy(port);
        return AWS_OP_SKIP;
    }

    Aws::Crt::ApiHandle apiHandle(allocator);
    Aws::Crt::Mqtt::MqttClient client;
    Aws::Crt::Io::SocketOptions socketOptions;
    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> connection =
        client.NewConnection(aws_string_c_str(endpoint), std::stoi(aws_string_c_str(port)), socketOptions, false);
    int connectResult = s_ConnectAndDisconnect(connection);
    if (connectResult != AWS_OP_SUCCESS)
    {
        ASSERT_TRUE(false);
    }
    aws_string_destroy(endpoint);
    aws_string_destroy(port);
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(Mqtt311DirectConnectionMinimal, s_TestMqtt311DirectConnectionMinimal)

/*
 * [ConnDC-UC2] Direct connection with basic authentication
 */
static int s_TestMqtt311DirectConnectionWithBasicAuth(Aws::Crt::Allocator *allocator, void *)
{
    struct aws_string *endpoint = NULL;
    struct aws_string *port = NULL;
    struct aws_string *username = NULL;
    struct aws_string *password = NULL;

    int error = aws_get_environment_value(allocator, s_mqtt311_test_envName_direct_basicauth_hostname, &endpoint);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_direct_basicauth_port, &port);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_basicauth_username, &username);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_basicauth_password, &password);

    bool isEveryEnvVarSet = (endpoint && port && username && password);
    if (isEveryEnvVarSet == true)
    {
        isEveryEnvVarSet =
            (aws_string_is_valid(endpoint) && aws_string_is_valid(port) && aws_string_is_valid(username) &&
             aws_string_is_valid(password));
    }
    if (error != AWS_OP_SUCCESS || isEveryEnvVarSet == false)
    {
        printf("Environment Variables are not set for the test, skip the test");
        aws_string_destroy(endpoint);
        aws_string_destroy(port);
        aws_string_destroy(username);
        aws_string_destroy(password);
        return AWS_OP_SKIP;
    }

    Aws::Crt::ApiHandle apiHandle(allocator);
    Aws::Crt::Mqtt::MqttClient client;
    Aws::Crt::Io::SocketOptions socketOptions;
    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> connection =
        client.NewConnection(aws_string_c_str(endpoint), std::stoi(aws_string_c_str(port)), socketOptions, false);
    connection->SetLogin(aws_string_c_str(username), aws_string_c_str(password));
    int connectResult = s_ConnectAndDisconnect(connection);
    if (connectResult != AWS_OP_SUCCESS)
    {
        ASSERT_TRUE(false);
    }
    aws_string_destroy(endpoint);
    aws_string_destroy(port);
    aws_string_destroy(username);
    aws_string_destroy(password);
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(Mqtt311DirectConnectionWithBasicAuth, s_TestMqtt311DirectConnectionWithBasicAuth)

/*
 * [ConnDC-UC3] Direct connection with TLS
 */
static int s_TestMqtt311DirectConnectionWithTLS(Aws::Crt::Allocator *allocator, void *)
{
    struct aws_string *endpoint = NULL;
    struct aws_string *port = NULL;

    int error = aws_get_environment_value(allocator, s_mqtt311_test_envName_direct_tls_hostname, &endpoint);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_direct_tls_port, &port);

    bool isEveryEnvVarSet = (endpoint && port);
    if (isEveryEnvVarSet == true)
    {
        isEveryEnvVarSet = (aws_string_is_valid(endpoint) && aws_string_is_valid(port));
    }
    if (error != AWS_OP_SUCCESS || isEveryEnvVarSet == false)
    {
        printf("Environment Variables are not set for the test, skip the test");
        aws_string_destroy(endpoint);
        aws_string_destroy(port);
        return AWS_OP_SKIP;
    }

    Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();
    ASSERT_TRUE(tlsCtxOptions);
    tlsCtxOptions.SetVerifyPeer(false);
    Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    ASSERT_TRUE(tlsContext);

    Aws::Crt::ApiHandle apiHandle(allocator);
    Aws::Crt::Mqtt::MqttClient client;
    Aws::Crt::Io::SocketOptions socketOptions;
    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> connection = client.NewConnection(
        aws_string_c_str(endpoint), std::stoi(aws_string_c_str(port)), socketOptions, tlsContext, false);
    int connectResult = s_ConnectAndDisconnect(connection);
    if (connectResult != AWS_OP_SUCCESS)
    {
        ASSERT_TRUE(false);
    }
    aws_string_destroy(endpoint);
    aws_string_destroy(port);
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(Mqtt311DirectConnectionWithTLS, s_TestMqtt311DirectConnectionWithTLS)

/*
 * [ConnDC-UC4] Direct connection with mutual TLS
 */
static int s_TestMqtt311DirectConnectionWithMutualTLS(Aws::Crt::Allocator *allocator, void *)
{
    struct aws_string *endpoint = NULL;
    struct aws_string *cert_path = NULL;
    struct aws_string *key_path = NULL;

    int error = aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_hostname, &endpoint);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_cert, &cert_path);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_key, &key_path);

    bool isEveryEnvVarSet = (endpoint && cert_path && key_path);
    if (isEveryEnvVarSet == true)
    {
        isEveryEnvVarSet =
            (aws_string_is_valid(endpoint) && aws_string_is_valid(cert_path) && aws_string_is_valid(key_path));
    }
    if (error != AWS_OP_SUCCESS || isEveryEnvVarSet == false)
    {
        printf("Environment Variables are not set for the test, skip the test");
        aws_string_destroy(endpoint);
        aws_string_destroy(cert_path);
        aws_string_destroy(key_path);
        return AWS_OP_SKIP;
    }

    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitClientWithMtls(
        aws_string_c_str(cert_path), aws_string_c_str(key_path), allocator);
    Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    ASSERT_TRUE(tlsContext);

    Aws::Crt::Mqtt::MqttClient client;
    Aws::Crt::Io::SocketOptions socketOptions;
    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> connection =
        client.NewConnection(aws_string_c_str(endpoint), 8883, socketOptions, tlsContext, false);
    int connectResult = s_ConnectAndDisconnect(connection);
    if (connectResult != AWS_OP_SUCCESS)
    {
        ASSERT_TRUE(false);
    }
    aws_string_destroy(endpoint);
    aws_string_destroy(cert_path);
    aws_string_destroy(key_path);
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(Mqtt311DirectConnectionWithMutualTLS, s_TestMqtt311DirectConnectionWithMutualTLS)

///*
// * [ConnDC-UC5] Direct connection with HttpProxy options
// */
static int s_TestMqtt311DirectConnectionWithHttpProxy(Aws::Crt::Allocator *allocator, void *)
{
    struct aws_string *endpoint = NULL;
    struct aws_string *port = NULL;
    struct aws_string *proxy_endpoint = NULL;
    struct aws_string *proxy_port = NULL;

    int error = aws_get_environment_value(allocator, s_mqtt311_test_envName_direct_tls_hostname, &endpoint);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_direct_tls_port, &port);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_proxy_hostname, &proxy_endpoint);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_proxy_port, &proxy_port);

    bool isEveryEnvVarSet = (endpoint && port && proxy_endpoint && proxy_port);
    if (isEveryEnvVarSet == true)
    {
        isEveryEnvVarSet =
            (aws_string_is_valid(endpoint) && aws_string_is_valid(port) && aws_string_is_valid(proxy_endpoint) &&
             aws_string_is_valid(proxy_port));
    }
    if (error != AWS_OP_SUCCESS || isEveryEnvVarSet == false)
    {
        printf("Environment Variables are not set for the test, skip the test");
        aws_string_destroy(endpoint);
        aws_string_destroy(port);
        return AWS_OP_SKIP;
    }

    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();
    ASSERT_TRUE(tlsCtxOptions);
    tlsCtxOptions.SetVerifyPeer(false);
    Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    ASSERT_TRUE(tlsContext);

    Aws::Crt::Http::HttpClientConnectionProxyOptions proxyOptions;
    proxyOptions.HostName = aws_string_c_str(proxy_endpoint);
    proxyOptions.Port = std::stoi(aws_string_c_str(proxy_port));
    proxyOptions.ProxyConnectionType = Aws::Crt::Http::AwsHttpProxyConnectionType::Tunneling;

    Aws::Crt::Mqtt::MqttClient client;
    Aws::Crt::Io::SocketOptions socketOptions;
    std::shared_ptr<Aws::Crt::Mqtt::MqttConnection> connection = client.NewConnection(
        aws_string_c_str(endpoint), std::stoi(aws_string_c_str(port)), socketOptions, tlsContext, false);
    connection->SetHttpProxyOptions(proxyOptions);
    int connectResult = s_ConnectAndDisconnect(connection);
    if (connectResult != AWS_OP_SUCCESS)
    {
        ASSERT_TRUE(false);
    }
    aws_string_destroy(endpoint);
    aws_string_destroy(port);
    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(Mqtt311DirectConnectionWithHttpProxy, s_TestMqtt311DirectConnectionWithHttpProxy)

//////////////////////////////////////////////////////////
// Websocket Connect Test Cases [ConnWS-UC]
//////////////////////////////////////////////////////////

// /*
//  * [ConnWS-UC1] Happy path. Websocket connection with minimal configuration.
//  */
// static int s_TestMqtt5WSConnectionMinimal(Aws::Crt::Allocator *allocator, void *)
// {
//     Mqtt5TestEnvVars mqtt5TestVars(allocator, MQTT5CONNECT_WS);
//     if (!mqtt5TestVars)
//     {
//         printf("Environment Variables are not set for the test, skip the test");
//         return AWS_OP_SKIP;
//     }

//     ApiHandle apiHandle(allocator);

//     Mqtt5::Mqtt5ClientOptions mqtt5Options(allocator);
//     mqtt5Options.WithHostName(mqtt5TestVars.m_hostname_string);
//     mqtt5Options.WithPort(mqtt5TestVars.m_port_value);

//     std::promise<bool> connectionPromise;
//     std::promise<void> stoppedPromise;

//     s_setupConnectionLifeCycle(mqtt5Options, connectionPromise, stoppedPromise);

//     Aws::Crt::Auth::CredentialsProviderChainDefaultConfig defaultConfig;
//     std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider =
//         Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(defaultConfig);

//     ASSERT_TRUE(provider);

//     Aws::Iot::WebsocketConfig config("us-east-1", provider);

//     mqtt5Options.WithWebsocketHandshakeTransformCallback(
//         [config](
//             std::shared_ptr<Aws::Crt::Http::HttpRequest> req,
//             const Aws::Crt::Mqtt5::OnWebSocketHandshakeInterceptComplete &onComplete) {
//             auto signingComplete =
//                 [onComplete](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &req1, int errorCode) {
//                     onComplete(req1, errorCode);
//                 };

//             auto signerConfig = config.CreateSigningConfigCb();

//             config.Signer->SignRequest(req, *signerConfig, signingComplete);
//         });

//     std::shared_ptr<Mqtt5::Mqtt5Client> mqtt5Client = Mqtt5::Mqtt5Client::NewMqtt5Client(mqtt5Options, allocator);
//     ASSERT_TRUE(mqtt5Client);
//     ASSERT_TRUE(mqtt5Client->Start());
//     connectionPromise.get_future().get();
//     ASSERT_TRUE(mqtt5Client->Stop());
//     stoppedPromise.get_future().get();
//     return AWS_OP_SUCCESS;
// }

// AWS_TEST_CASE(Mqtt5WSConnectionMinimal, s_TestMqtt5WSConnectionMinimal)

// /*
//  * [ConnWS-UC2] websocket connection with basic authentication
//  */
// static int s_TestMqtt5WSConnectionWithBasicAuth(Aws::Crt::Allocator *allocator, void *)
// {
//     Mqtt5TestEnvVars mqtt5TestVars(allocator, MQTT5CONNECT_WS_BASIC_AUTH);
//     if (!mqtt5TestVars)
//     {
//         printf("Environment Variables are not set for the test, skip the test");
//         return AWS_OP_SKIP;
//     }

//     ApiHandle apiHandle(allocator);

//     Mqtt5::Mqtt5ClientOptions mqtt5Options(allocator);
//     mqtt5Options.WithHostName(mqtt5TestVars.m_hostname_string);
//     mqtt5Options.WithPort(mqtt5TestVars.m_port_value);

//     std::shared_ptr<Aws::Crt::Mqtt5::ConnectPacket> packetConnect =
//     std::make_shared<Aws::Crt::Mqtt5::ConnectPacket>(); packetConnect->WithUserName(mqtt5TestVars.m_username_string);
//     packetConnect->WithPassword(mqtt5TestVars.m_password_cursor);
//     mqtt5Options.WithConnectOptions(packetConnect);

//     std::promise<bool> connectionPromise;
//     std::promise<void> stoppedPromise;

//     s_setupConnectionLifeCycle(mqtt5Options, connectionPromise, stoppedPromise);

//     Aws::Crt::Auth::CredentialsProviderChainDefaultConfig defaultConfig;
//     std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider =
//         Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(defaultConfig);

//     ASSERT_TRUE(provider);

//     Aws::Iot::WebsocketConfig config("us-east-1", provider);

//     mqtt5Options.WithWebsocketHandshakeTransformCallback(
//         [config](
//             std::shared_ptr<Aws::Crt::Http::HttpRequest> req,
//             const Aws::Crt::Mqtt5::OnWebSocketHandshakeInterceptComplete &onComplete) {
//             auto signingComplete =
//                 [onComplete](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &req1, int errorCode) {
//                     onComplete(req1, errorCode);
//                 };

//             auto signerConfig = config.CreateSigningConfigCb();

//             config.Signer->SignRequest(req, *signerConfig, signingComplete);
//         });

//     std::shared_ptr<Mqtt5::Mqtt5Client> mqtt5Client = Mqtt5::Mqtt5Client::NewMqtt5Client(mqtt5Options, allocator);
//     ASSERT_TRUE(mqtt5Client);

//     ASSERT_TRUE(mqtt5Client->Start());
//     ASSERT_TRUE(connectionPromise.get_future().get());
//     ASSERT_TRUE(mqtt5Client->Stop());
//     stoppedPromise.get_future().get();
//     return AWS_OP_SUCCESS;
// }

// AWS_TEST_CASE(Mqtt5WSConnectionWithBasicAuth, s_TestMqtt5WSConnectionWithBasicAuth)

// /*
//  * [ConnWS-UC3] websocket connection with TLS
//  */
// static int s_TestMqtt5WSConnectionWithTLS(Aws::Crt::Allocator *allocator, void *)
// {
//     Mqtt5TestEnvVars mqtt5TestVars(allocator, MQTT5CONNECT_WS_TLS);
//     if (!mqtt5TestVars)
//     {
//         printf("Environment Variables are not set for the test, skip the test");
//         return AWS_OP_SKIP;
//     }

//     ApiHandle apiHandle(allocator);

//     Mqtt5::Mqtt5ClientOptions mqtt5Options(allocator);
//     mqtt5Options.WithHostName(mqtt5TestVars.m_hostname_string);
//     mqtt5Options.WithPort(mqtt5TestVars.m_port_value);

//     std::promise<bool> connectionPromise;
//     std::promise<void> stoppedPromise;

//     s_setupConnectionLifeCycle(mqtt5Options, connectionPromise, stoppedPromise);

//     Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();

//     Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
//     ASSERT_TRUE(tlsContext);
//     Aws::Crt::Io::TlsConnectionOptions tlsConnection = tlsContext.NewConnectionOptions();

//     ASSERT_TRUE(tlsConnection);
//     mqtt5Options.WithTlsConnectionOptions(tlsConnection);

//     // setup websocket config
//     Aws::Crt::Auth::CredentialsProviderChainDefaultConfig defaultConfig;
//     std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider =
//         Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(defaultConfig);

//     ASSERT_TRUE(provider);

//     Aws::Iot::WebsocketConfig config("us-east-1", provider);

//     mqtt5Options.WithWebsocketHandshakeTransformCallback(
//         [config](
//             std::shared_ptr<Aws::Crt::Http::HttpRequest> req,
//             const Aws::Crt::Mqtt5::OnWebSocketHandshakeInterceptComplete &onComplete) {
//             auto signingComplete =
//                 [onComplete](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &req1, int errorCode) {
//                     onComplete(req1, errorCode);
//                 };

//             auto signerConfig = config.CreateSigningConfigCb();

//             config.Signer->SignRequest(req, *signerConfig, signingComplete);
//         });

//     std::shared_ptr<Mqtt5::Mqtt5Client> mqtt5Client = Mqtt5::Mqtt5Client::NewMqtt5Client(mqtt5Options, allocator);
//     ASSERT_TRUE(mqtt5Client);
//     ASSERT_TRUE(mqtt5Client->Start());
//     connectionPromise.get_future().get();
//     ASSERT_TRUE(mqtt5Client->Stop());
//     stoppedPromise.get_future().get();
//     return AWS_OP_SUCCESS;
// }

// AWS_TEST_CASE(Mqtt5WSConnectionWithTLS, s_TestMqtt5WSConnectionWithTLS)

// /*
//  * [ConnDC-UC4] Websocket connection with mutual TLS
//  */

// static int s_TestMqtt5WSConnectionWithMutualTLS(Aws::Crt::Allocator *allocator, void *)
// {
//     Mqtt5TestEnvVars mqtt5TestVars(allocator, MQTT5CONNECT_IOT_CORE);
//     if (!mqtt5TestVars)
//     {
//         printf("Environment Variables are not set for the test, skip the test");
//         return AWS_OP_SKIP;
//     }

//     ApiHandle apiHandle(allocator);

//     Mqtt5::Mqtt5ClientOptions mqtt5Options(allocator);
//     mqtt5Options.WithHostName(mqtt5TestVars.m_hostname_string);
//     mqtt5Options.WithPort(443);

//     std::promise<bool> connectionPromise;
//     std::promise<void> stoppedPromise;

//     s_setupConnectionLifeCycle(mqtt5Options, connectionPromise, stoppedPromise);

//     Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitClientWithMtls(
//         mqtt5TestVars.m_certificate_path_string.c_str(), mqtt5TestVars.m_private_key_path_string.c_str(), allocator);

//     Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
//     ASSERT_TRUE(tlsContext);
//     Aws::Crt::Io::TlsConnectionOptions tlsConnection = tlsContext.NewConnectionOptions();

//     ASSERT_TRUE(tlsConnection);
//     mqtt5Options.WithTlsConnectionOptions(tlsConnection);

//     // setup websocket config
//     Aws::Crt::Auth::CredentialsProviderChainDefaultConfig defaultConfig;
//     std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider =
//         Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(defaultConfig);

//     ASSERT_TRUE(provider);

//     Aws::Iot::WebsocketConfig config("us-east-1", provider);

//     mqtt5Options.WithWebsocketHandshakeTransformCallback(
//         [config](
//             std::shared_ptr<Aws::Crt::Http::HttpRequest> req,
//             const Aws::Crt::Mqtt5::OnWebSocketHandshakeInterceptComplete &onComplete) {
//             auto signingComplete =
//                 [onComplete](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &req1, int errorCode) {
//                     onComplete(req1, errorCode);
//                 };

//             auto signerConfig = config.CreateSigningConfigCb();

//             config.Signer->SignRequest(req, *signerConfig, signingComplete);
//         });

//     std::shared_ptr<Mqtt5::Mqtt5Client> mqtt5Client = Mqtt5::Mqtt5Client::NewMqtt5Client(mqtt5Options, allocator);
//     ASSERT_TRUE(mqtt5Client);
//     ASSERT_TRUE(mqtt5Client->Start());
//     connectionPromise.get_future().get();
//     ASSERT_TRUE(mqtt5Client->Stop());
//     stoppedPromise.get_future().get();
//     return AWS_OP_SUCCESS;
// }

// AWS_TEST_CASE(Mqtt5WSConnectionWithMutualTLS, s_TestMqtt5WSConnectionWithMutualTLS)

// /*
//  * ConnWS-UC5] Websocket connection with HttpProxy options
//  *
//  */
// static int s_TestMqtt5WSConnectionWithHttpProxy(Aws::Crt::Allocator *allocator, void *)
// {
//     Mqtt5TestEnvVars mqtt5TestVars(allocator, MQTT5CONNECT_WS_TLS);
//     if (!mqtt5TestVars)
//     {
//         printf("Environment Variables are not set for the test, skip the test");
//         return AWS_OP_SKIP;
//     }

//     ApiHandle apiHandle(allocator);

//     Mqtt5::Mqtt5ClientOptions mqtt5Options(allocator);
//     mqtt5Options.WithHostName(mqtt5TestVars.m_hostname_string);
//     mqtt5Options.WithPort(443);

//     // HTTP PROXY
//     if (mqtt5TestVars.m_httpproxy_hostname->len == 0)
//     {
//         printf("HTTP PROXY Environment Variables are not set for the test, skip the test");
//         return AWS_OP_SUCCESS;
//     }
//     Aws::Crt::Http::HttpClientConnectionProxyOptions proxyOptions;
//     proxyOptions.HostName = mqtt5TestVars.m_httpproxy_hostname_string;
//     proxyOptions.Port = mqtt5TestVars.m_httpproxy_port_value;
//     proxyOptions.AuthType = Aws::Crt::Http::AwsHttpProxyAuthenticationType::None;
//     proxyOptions.ProxyConnectionType = Aws::Crt::Http::AwsHttpProxyConnectionType::Tunneling;
//     mqtt5Options.WithHttpProxyOptions(proxyOptions);

//     // TLS
//     Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();
//     tlsCtxOptions.SetVerifyPeer(false);
//     Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
//     ASSERT_TRUE(tlsContext);
//     Aws::Crt::Io::TlsConnectionOptions tlsConnection = tlsContext.NewConnectionOptions();
//     ASSERT_TRUE(tlsConnection);
//     mqtt5Options.WithTlsConnectionOptions(tlsConnection);

//     // setup websocket config
//     Aws::Crt::Auth::CredentialsProviderChainDefaultConfig defaultConfig;
//     std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider =
//         Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(defaultConfig);

//     ASSERT_TRUE(provider);

//     Aws::Iot::WebsocketConfig config("us-east-1", provider);
//     config.ProxyOptions = proxyOptions;

//     mqtt5Options.WithWebsocketHandshakeTransformCallback(
//         [config](
//             std::shared_ptr<Aws::Crt::Http::HttpRequest> req,
//             const Aws::Crt::Mqtt5::OnWebSocketHandshakeInterceptComplete &onComplete) {
//             auto signingComplete =
//                 [onComplete](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &req1, int errorCode) {
//                     onComplete(req1, errorCode);
//                 };

//             auto signerConfig = config.CreateSigningConfigCb();

//             config.Signer->SignRequest(req, *signerConfig, signingComplete);
//         });

//     std::promise<bool> connectionPromise;
//     std::promise<void> stoppedPromise;

//     s_setupConnectionLifeCycle(mqtt5Options, connectionPromise, stoppedPromise);

//     std::shared_ptr<Mqtt5::Mqtt5Client> mqtt5Client = Mqtt5::Mqtt5Client::NewMqtt5Client(mqtt5Options, allocator);
//     ASSERT_TRUE(mqtt5Client);

//     ASSERT_TRUE(mqtt5Client->Start());
//     ASSERT_TRUE(connectionPromise.get_future().get());
//     ASSERT_TRUE(mqtt5Client->Stop());
//     stoppedPromise.get_future().get();
//     return AWS_OP_SUCCESS;
// }

// AWS_TEST_CASE(Mqtt5WSConnectionWithHttpProxy, s_TestMqtt5WSConnectionWithHttpProxy)

// /*
//  * [ConnDC-UC6] Direct connection with all options set
//  */
// static int s_TestMqtt5WSConnectionFull(Aws::Crt::Allocator *allocator, void *)
// {
//     Mqtt5TestEnvVars mqtt5TestVars(allocator, MQTT5CONNECT_WS);
//     if (!mqtt5TestVars)
//     {
//         printf("Environment Variables are not set for the test, skip the test");
//         return AWS_OP_SKIP;
//     }

//     ApiHandle apiHandle(allocator);

//     Mqtt5::Mqtt5ClientOptions mqtt5Options(allocator);
//     mqtt5Options.WithHostName(mqtt5TestVars.m_hostname_string).WithPort(mqtt5TestVars.m_port_value);

//     Aws::Crt::Io::SocketOptions socketOptions;
//     socketOptions.SetConnectTimeoutMs(3000);

//     Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
//     ASSERT_TRUE(eventLoopGroup);

//     Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
//     ASSERT_TRUE(defaultHostResolver);

//     Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
//     ASSERT_TRUE(allocator);
//     clientBootstrap.EnableBlockingShutdown();

//     // Setup will
//     const Aws::Crt::String TEST_TOPIC =
//         "test/MQTT5_Binding_CPP/s_TestMqtt5WSConnectionFull" + Aws::Crt::UUID().ToString();
//     ByteBuf will_payload = Aws::Crt::ByteBufFromCString("Will Test");
//     std::shared_ptr<Mqtt5::PublishPacket> will = std::make_shared<Mqtt5::PublishPacket>(
//         TEST_TOPIC, ByteCursorFromByteBuf(will_payload), Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE, allocator);

//     std::shared_ptr<Aws::Crt::Mqtt5::ConnectPacket> packetConnect =
//     std::make_shared<Aws::Crt::Mqtt5::ConnectPacket>(); packetConnect->WithClientId("s_TestMqtt5WSConnectionFull" +
//     Aws::Crt::UUID().ToString())
//         .WithKeepAliveIntervalSec(1000)
//         .WithMaximumPacketSizeBytes(1000L)
//         .WithReceiveMaximum(1000)
//         .WithRequestProblemInformation(true)
//         .WithRequestResponseInformation(true)
//         .WithSessionExpiryIntervalSec(1000L)
//         .WithWill(will)
//         .WithWillDelayIntervalSec(1000);

//     Aws::Crt::Mqtt5::UserProperty userProperty("PropertyName", "PropertyValue");
//     packetConnect->WithUserProperty(std::move(userProperty));

//     Aws::Crt::Mqtt5::ReconnectOptions reconnectOptions = {
//         Mqtt5::JitterMode::AWS_EXPONENTIAL_BACKOFF_JITTER_FULL, 1000, 1000, 1000};

//     mqtt5Options.WithConnectOptions(packetConnect);
//     mqtt5Options.WithBootstrap(&clientBootstrap);
//     mqtt5Options.WithSocketOptions(socketOptions);
//     mqtt5Options.WithSessionBehavior(Mqtt5::ClientSessionBehaviorType::AWS_MQTT5_CSBT_REJOIN_POST_SUCCESS);
//     mqtt5Options.WithClientExtendedValidationAndFlowControl(
//         Mqtt5::ClientExtendedValidationAndFlowControl::AWS_MQTT5_EVAFCO_NONE);
//     mqtt5Options.WithOfflineQueueBehavior(
//         Mqtt5::ClientOperationQueueBehaviorType::AWS_MQTT5_COQBT_FAIL_QOS0_PUBLISH_ON_DISCONNECT);
//     mqtt5Options.WithReconnectOptions(reconnectOptions);
//     mqtt5Options.WithPingTimeoutMs(1000);
//     mqtt5Options.WithConnackTimeoutMs(100);
//     mqtt5Options.WithAckTimeoutSeconds(1000);

//     std::promise<bool> connectionPromise;
//     std::promise<void> stoppedPromise;

//     s_setupConnectionLifeCycle(mqtt5Options, connectionPromise, stoppedPromise);

//     // setup websocket config
//     Aws::Crt::Auth::CredentialsProviderChainDefaultConfig defaultConfig;
//     std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> provider =
//         Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(defaultConfig);

//     ASSERT_TRUE(provider);

//     Aws::Iot::WebsocketConfig config("us-east-1", provider);

//     mqtt5Options.WithWebsocketHandshakeTransformCallback(
//         [config](
//             std::shared_ptr<Aws::Crt::Http::HttpRequest> req,
//             const Aws::Crt::Mqtt5::OnWebSocketHandshakeInterceptComplete &onComplete) {
//             auto signingComplete =
//                 [onComplete](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &req1, int errorCode) {
//                     onComplete(req1, errorCode);
//                 };

//             auto signerConfig = config.CreateSigningConfigCb();

//             config.Signer->SignRequest(req, *signerConfig, signingComplete);
//         });

//     std::shared_ptr<Mqtt5::Mqtt5Client> mqtt5Client = Mqtt5::Mqtt5Client::NewMqtt5Client(mqtt5Options, allocator);
//     ASSERT_TRUE(mqtt5Client);
//     ASSERT_TRUE(mqtt5Client->Start());
//     ASSERT_TRUE(connectionPromise.get_future().get());
//     ASSERT_TRUE(mqtt5Client->Stop());
//     stoppedPromise.get_future().get();
//     return AWS_OP_SUCCESS;
// }
// AWS_TEST_CASE(Mqtt5WSConnectionFull, s_TestMqtt5WSConnectionFull)
