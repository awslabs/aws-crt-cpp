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

AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_iot_hostname, "AWS_TEST_MQTT311_IOT_CORE_HOST");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt311_test_envName_iot_nosign_custom_auth_name,
    "AWS_TEST_MQTT311_IOT_CORE_NO_SIGNING_AUTHORIZER_NAME");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt311_test_envName_iot_nosign_custom_auth_username,
    "AWS_TEST_MQTT311_IOT_CORE_NO_SIGNING_AUTHORIZER_USERNAME");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt311_test_envName_iot_nosign_custom_auth_password,
    "AWS_TEST_MQTT311_IOT_CORE_NO_SIGNING_AUTHORIZER_PASSWORD");

AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt311_test_envName_iot_sign_custom_auth_name,
    "AWS_TEST_MQTT311_IOT_CORE_SIGNING_AUTHORIZER_NAME");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt311_test_envName_iot_sign_custom_auth_username,
    "AWS_TEST_MQTT311_IOT_CORE_SIGNING_AUTHORIZER_USERNAME");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt311_test_envName_iot_sign_custom_auth_password,
    "AWS_TEST_MQTT311_IOT_CORE_SIGNING_AUTHORIZER_PASSWORD");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt311_test_envName_iot_sign_custom_auth_tokenvalue,
    "AWS_TEST_MQTT311_IOT_CORE_SIGNING_AUTHORIZER_TOKEN");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt311_test_envName_iot_sign_custom_auth_tokenkey,
    "AWS_TEST_MQTT311_IOT_CORE_SIGNING_AUTHORIZER_TOKEN_KEY_NAME");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt311_test_envName_iot_sign_custom_auth_tokensignature,
    "AWS_TEST_MQTT311_IOT_CORE_SIGNING_AUTHORIZER_TOKEN_SIGNATURE");

AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_iot_pkcs12_key, "AWS_TEST_MQTT311_IOT_CORE_PKCS12_KEY");
AWS_STATIC_STRING_FROM_LITERAL(
    s_mqtt311_test_envName_iot_pkcs12_key_password,
    "AWS_TEST_MQTT311_IOT_CORE_PKCS12_KEY_PASSWORD");

// Needed to return "success" instead of skip in Codebuild so it doesn't count as a failure
AWS_STATIC_STRING_FROM_LITERAL(s_mqtt311_test_envName_codebuild, "CODEBUILD_BUILD_ID");

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

    int error = aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_hostname, &endpoint);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_nosign_custom_auth_name, &authname);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_nosign_custom_auth_username, &username);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_nosign_custom_auth_password, &password);

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

    int error = aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_hostname, &endpoint);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_sign_custom_auth_name, &authname);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_sign_custom_auth_username, &username);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_sign_custom_auth_password, &password);
    error |=
        aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_sign_custom_auth_tokensignature, &signature);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_sign_custom_auth_tokenkey, &tokenKeyName);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_sign_custom_auth_tokenvalue, &tokenValue);

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
    struct aws_string *codebuild_buildID = NULL;

    int error = aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_hostname, &endpoint);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_pkcs12_key, &pkcs12_key);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_iot_pkcs12_key_password, &pkcs12_password);
    error |= aws_get_environment_value(allocator, s_mqtt311_test_envName_codebuild, &codebuild_buildID);

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

        // If in Codebuild, return as a 'success' even though it is a skip
        if (codebuild_buildID && aws_string_is_valid(codebuild_buildID))
        {
            aws_string_destroy(codebuild_buildID);
            return AWS_OP_SUCCESS;
        }
        aws_string_destroy(codebuild_buildID);

        return AWS_OP_SKIP;
    }

    Aws::Crt::ApiHandle apiHandle(allocator);

    Aws::Iot::Pkcs12Options testPkcs12Options;
    testPkcs12Options.pkcs12_file = aws_string_c_str(pkcs12_key);
    testPkcs12Options.pkcs12_password = aws_string_c_str(pkcs12_password);

    Aws::Iot::MqttClient client;
    auto clientConfigBuilder = Aws::Iot::MqttClientConnectionConfigBuilder(testPkcs12Options, allocator);
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
    aws_string_destroy(codebuild_buildID);
    return AWS_OP_SUCCESS;
}
AWS_TEST_CASE(IoTMqtt311ConnectWithPKCS12, s_TestIoTMqtt311ConnectWithPKCS12)
