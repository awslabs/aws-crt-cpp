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

/**
 * NOTE: Will later add more testing and split MQTT5 testing setup from MQTT311 testing setup.
 * In the short term, the MQTT311 tests will reuse the majority of MQTT5 material.
 */

AWS_STATIC_STRING_FROM_LITERAL(s_mqtt5_test_envName_iot_hostname, "AWS_TEST_MQTT5_IOT_CORE_HOST");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt5_test_envName_iot_nosign_custom_auth_name,
    "AWS_TEST_MQTT5_IOT_CORE_NO_SIGNING_AUTHORIZER_NAME");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt5_test_envName_iot_nosign_custom_auth_username,
    "AWS_TEST_MQTT5_IOT_CORE_NO_SIGNING_AUTHORIZER_USERNAME");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt5_test_envName_iot_nosign_custom_auth_password,
    "AWS_TEST_MQTT5_IOT_CORE_NO_SIGNING_AUTHORIZER_PASSWORD");

AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt5_test_envName_iot_sign_custom_auth_name,
    "AWS_TEST_MQTT5_IOT_CORE_SIGNING_AUTHORIZER_NAME");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt5_test_envName_iot_sign_custom_auth_username,
    "AWS_TEST_MQTT5_IOT_CORE_SIGNING_AUTHORIZER_USERNAME");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt5_test_envName_iot_sign_custom_auth_password,
    "AWS_TEST_MQTT5_IOT_CORE_SIGNING_AUTHORIZER_PASSWORD");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt5_test_envName_iot_sign_custom_auth_tokenvalue,
    "AWS_TEST_MQTT5_IOT_CORE_SIGNING_AUTHORIZER_TOKEN");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt5_test_envName_iot_sign_custom_auth_tokenkey,
    "AWS_TEST_MQTT5_IOT_CORE_SIGNING_AUTHORIZER_TOKEN_KEY_NAME");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt5_test_envName_iot_sign_custom_auth_tokensignature,
    "AWS_TEST_MQTT5_IOT_CORE_SIGNING_AUTHORIZER_TOKEN_SIGNATURE");

AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_iot_pkcs12_key, "AWS_TEST_MQTT311_IOT_CORE_PKCS12_KEY");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt311_test_envName_iot_pkcs12_key_password,
    "AWS_TEST_MQTT311_IOT_CORE_PKCS12_KEY_PASSWORD");

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

/*
 * Custom Auth (no signing) connect for MQTT311
 */
static int s_TestIoTMqtt311ConnectWithNoSigningCustomAuth(Aws::Crt::Allocator *allocator, void *)
{
    struct aws_string *endpoint = NULL;
    struct aws_string *authname = NULL;
    struct aws_string *username = NULL;
    struct aws_string *password = NULL;
    struct aws_string *empty_string = aws_string_new_from_c_str(allocator, "");

    int error = aws_get_environment_value(allocator, s_mqtt5_test_envName_iot_hostname, &endpoint);
    error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_iot_nosign_custom_auth_name, &authname);
    error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_iot_nosign_custom_auth_username, &username);
    error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_iot_nosign_custom_auth_password, &password);

    bool isEveryEnvVarSet = (endpoint && authname && username && password);
    if (isEveryEnvVarSet == true)
    {
        isEveryEnvVarSet =
            (aws_string_is_valid(endpoint) && aws_string_is_valid(authname) && aws_string_is_valid(username) &&
             aws_string_is_valid(password));
    }
    if (error != AWS_OP_SUCCESS || isEveryEnvVarSet == false)
    {
        printf("Environment Variables are not set for the test, skip the test");
        aws_string_destroy(endpoint);
        aws_string_destroy(authname);
        aws_string_destroy(username);
        aws_string_destroy(password);
        aws_string_destroy(empty_string);
        return AWS_OP_SKIP;
    }

    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Iot::MqttClient client;
    auto clientConfigBuilder = Aws::Iot::MqttClientConnectionConfigBuilder::NewDefaultBuilder();
    clientConfigBuilder.WithEndpoint(aws_string_c_str(endpoint));
    clientConfigBuilder.WithCustomAuthorizer(
        aws_string_c_str(username),
        aws_string_c_str(authname),
        aws_string_c_str(empty_string),
        aws_string_c_str(password));
    auto clientConfig = clientConfigBuilder.Build();
    if (!clientConfig)
    {
        printf("Failed to create MQTT311 client from config");
        ASSERT_TRUE(false);
    }
    auto connection = client.NewConnection(clientConfig);
    if (!*connection)
    {
        printf("Failed to create MQTT311 connection from config");
        ASSERT_TRUE(false);
    }

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
        ASSERT_TRUE(false);
    }
    if (connectionCompletedPromise.get_future().get() == false)
    {
        printf("Connection failed");
        ASSERT_TRUE(false);
    }
    if (connection->Disconnect())
    {
        connectionClosedPromise.get_future().wait();
    }

    aws_string_destroy(endpoint);
    aws_string_destroy(authname);
    aws_string_destroy(username);
    aws_string_destroy(password);
    aws_string_destroy(empty_string);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTMqtt311ConnectWithNoSigningCustomAuth, s_TestIoTMqtt311ConnectWithNoSigningCustomAuth)

/*
 * Custom Auth (signing) connect for MQTT311
 */
static int s_TestIoTMqtt311ConnectWithSigningCustomAuth(Aws::Crt::Allocator *allocator, void *)
{
    struct aws_string *endpoint = NULL;
    struct aws_string *authname = NULL;
    struct aws_string *username = NULL;
    struct aws_string *password = NULL;
    struct aws_string *signature = NULL;
    struct aws_string *tokenKeyName = NULL;
    struct aws_string *tokenValue = NULL;

    int error = aws_get_environment_value(allocator, s_mqtt5_test_envName_iot_hostname, &endpoint);
    error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_iot_sign_custom_auth_name, &authname);
    error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_iot_sign_custom_auth_username, &username);
    error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_iot_sign_custom_auth_password, &password);
    error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_iot_sign_custom_auth_tokensignature, &signature);
    error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_iot_sign_custom_auth_tokenkey, &tokenKeyName);
    error |= aws_get_environment_value(allocator, s_mqtt5_test_envName_iot_sign_custom_auth_tokenvalue, &tokenValue);

    bool isEveryEnvVarSet = (endpoint && authname && username && password && signature && tokenKeyName && tokenValue);
    if (isEveryEnvVarSet == true)
    {
        isEveryEnvVarSet =
            (aws_string_is_valid(endpoint) && aws_string_is_valid(authname) && aws_string_is_valid(username) &&
             aws_string_is_valid(password) && aws_string_is_valid(signature) && aws_string_is_valid(tokenKeyName) &&
             aws_string_is_valid(tokenValue));
    }
    if (error != AWS_OP_SUCCESS || isEveryEnvVarSet == false)
    {
        printf("Environment Variables are not set for the test, skip the test");
        aws_string_destroy(endpoint);
        aws_string_destroy(authname);
        aws_string_destroy(username);
        aws_string_destroy(password);
        aws_string_destroy(signature);
        aws_string_destroy(tokenKeyName);
        aws_string_destroy(tokenValue);
        return AWS_OP_SKIP;
    }

    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Iot::MqttClient client;
    auto clientConfigBuilder = Aws::Iot::MqttClientConnectionConfigBuilder::NewDefaultBuilder();
    clientConfigBuilder.WithEndpoint(aws_string_c_str(endpoint));
    clientConfigBuilder.WithCustomAuthorizer(
        aws_string_c_str(username),
        aws_string_c_str(authname),
        aws_string_c_str(signature),
        aws_string_c_str(password),
        aws_string_c_str(tokenKeyName),
        aws_string_c_str(tokenValue));
    auto clientConfig = clientConfigBuilder.Build();
    if (!clientConfig)
    {
        printf("Failed to create MQTT311 client from config");
        ASSERT_TRUE(false);
    }
    auto connection = client.NewConnection(clientConfig);
    if (!*connection)
    {
        printf("Failed to create MQTT311 connection from config");
        ASSERT_TRUE(false);
    }

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
        ASSERT_TRUE(false);
    }
    if (connectionCompletedPromise.get_future().get() == false)
    {
        printf("Connection failed");
        ASSERT_TRUE(false);
    }
    if (connection->Disconnect())
    {
        connectionClosedPromise.get_future().wait();
    }

    aws_string_destroy(endpoint);
    aws_string_destroy(authname);
    aws_string_destroy(username);
    aws_string_destroy(password);
    aws_string_destroy(signature);
    aws_string_destroy(tokenKeyName);
    aws_string_destroy(tokenValue);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTMqtt311ConnectWithSigningCustomAuth, s_TestIoTMqtt311ConnectWithSigningCustomAuth)

/*
 * PKCS12 connect for MQTT311
 */
static int s_TestIoTMqtt311ConnectWithPKCS12(Aws::Crt::Allocator *allocator, void *)
{
    struct aws_string *endpoint = NULL;
    struct aws_string *pkcs12_key = NULL;
    struct aws_string *pkcs12_password = NULL;

    int error = aws_get_environment_value(allocator, s_mqtt5_test_envName_iot_hostname, &endpoint);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_pkcs12_key, &pkcs12_key);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_pkcs12_key_password, &pkcs12_password);

    bool isEveryEnvVarSet = (endpoint && pkcs12_key && pkcs12_password);
    if (isEveryEnvVarSet == true)
    {
        isEveryEnvVarSet =
            (aws_string_is_valid(endpoint) && aws_string_is_valid(pkcs12_key) && aws_string_is_valid(pkcs12_password));
    }
    if (error != AWS_OP_SUCCESS || isEveryEnvVarSet == false)
    {
        printf("Environment Variables are not set for the test, skip the test");
        aws_string_destroy(endpoint);
        aws_string_destroy(pkcs12_key);
        aws_string_destroy(pkcs12_password);
        return AWS_OP_SKIP;
    }

    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Iot::Pkcs12Options testPkcs12Options =
        {.pkcs12_file = aws_string_c_str(pkcs12_key), .pkcs12_password = aws_string_c_str(pkcs12_password)};

    Aws::Iot::MqttClient client;
    auto clientConfigBuilder =
        Aws::Iot::MqttClientConnectionConfigBuilder(testPkcs12Options, allocator);
    clientConfigBuilder.WithEndpoint(aws_string_c_str(endpoint));
    auto clientConfig = clientConfigBuilder.Build();
    if (!clientConfig)
    {
        printf("Failed to create MQTT311 client from config");
        ASSERT_TRUE(false);
    }
    auto connection = client.NewConnection(clientConfig);
    if (!*connection)
    {
        printf("Failed to create MQTT311 connection from config");
        ASSERT_TRUE(false);
    }

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
        ASSERT_TRUE(false);
    }
    if (connectionCompletedPromise.get_future().get() == false)
    {
        printf("Connection failed");
        ASSERT_TRUE(false);
    }
    if (connection->Disconnect())
    {
        connectionClosedPromise.get_future().wait();
    }

    aws_string_destroy(endpoint);
    aws_string_destroy(pkcs12_key);
    aws_string_destroy(pkcs12_password);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTMqtt311ConnectWithPKCS12, s_TestIoTMqtt311ConnectWithPKCS12)
