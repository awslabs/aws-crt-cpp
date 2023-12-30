/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Api.h>
#include <aws/crt/crypto/Hash.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/Uri.h>

#include <aws/crt/mqtt/Mqtt5Packets.h>

#include <aws/common/command_line_parser.h>
#include <condition_variable>
#include <fstream>
#include <future>
#include <iostream>

#define AWS_MQTT5_CANARY_CLIENT_CREATION_SLEEP_TIME 10000000
#define AWS_MQTT5_CANARY_OPERATION_ARRAY_SIZE 10000
#define AWS_MQTT5_CANARY_TOPIC_ARRAY_SIZE 256
#define AWS_MQTT5_CANARY_CLIENT_MAX 50
#define AWS_MQTT5_CANARY_PAYLOAD_SIZE_MAX UINT16_MAX

using namespace Aws::Crt;
using namespace Aws::Crt::Mqtt5;

struct app_ctx
{
    struct aws_allocator *allocator;
    Io::Uri uri;
    uint32_t port;
    const char *cacert;
    const char *cert;
    const char *key;
    int connect_timeout;

    struct aws_tls_connection_options tls_connection_options;

    const char *TraceFile;
    Aws::Crt::LogLevel LogLevel;
};

static void s_usage(int exit_code)
{

    fprintf(stderr, "usage: elastipubsub5 [options] endpoint\n");
    fprintf(stderr, " endpoint: url to connect to\n");
    fprintf(stderr, "\n Options:\n\n");
    fprintf(stderr, "      --cacert FILE: path to a CA certficate file.\n");
    fprintf(stderr, "      --cert FILE: path to a PEM encoded certificate to use with mTLS\n");
    fprintf(stderr, "      --key FILE: Path to a PEM encoded private key that matches cert.\n");
    fprintf(stderr, "  -l, --log FILE: dumps logs to FILE instead of stderr.\n");
    fprintf(stderr, "  -v, --verbose: ERROR|INFO|DEBUG|TRACE: log level to configure. Default is none.\n");

    fprintf(stderr, "  -h, --help\n");
    fprintf(stderr, "            Display this message and quit.\n");
    exit(exit_code);
}

static struct aws_cli_option s_long_options[] = {
    {"cacert", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'a'},
    {"cert", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'c'},
    {"key", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'e'},
    {"connect-timeout", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'f'},
    {"log", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'l'},
    {"verbose", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'v'},
    {"help", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'h'},
    /* Per getopt(3) the last element of the array has to be filled with all zeros */
    {NULL, AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 0},
};

static void s_parse_options(int argc, char **argv, struct app_ctx &ctx)
{
    while (true)
    {
        int option_index = 0;
        int c = aws_cli_getopt_long(argc, argv, "a:b:c:e:f:H:d:g:M:GPHiko:t:v:VwWh", s_long_options, &option_index);
        if (c == -1)
        {
            /* finished parsing */
            break;
        }

        switch (c)
        {
            case 0:
                /* getopt_long() returns 0 if an option.flag is non-null */
                break;
            case 0x02:
                /* getopt_long() returns 0x02 (START_OF_TEXT) if a positional arg was encountered */
                ctx.uri = Io::Uri(aws_byte_cursor_from_c_str(aws_cli_positional_arg), ctx.allocator);
                if (!ctx.uri)
                {
                    std::cerr << "Failed to parse uri \"" << aws_cli_positional_arg << "\" with error "
                              << aws_error_debug_str(ctx.uri.LastError()) << std::endl;
                    s_usage(1);
                }
                else
                {
                    std::cerr << "Success to parse uri \"" << aws_cli_positional_arg
                              << static_cast<const char *>(AWS_BYTE_CURSOR_PRI(ctx.uri.GetFullUri())) << std::endl;
                }
                break;
            case 'a':
                ctx.cacert = aws_cli_optarg;
                break;
            case 'c':
                ctx.cert = aws_cli_optarg;
                break;
            case 'e':
                ctx.key = aws_cli_optarg;
                break;
            case 't':
                ctx.TraceFile = aws_cli_optarg;
                break;
            case 'h':
                s_usage(0);
                break;
            case 'v':
            {
                enum aws_log_level temp_log_level = AWS_LL_NONE;
                aws_string_to_log_level(aws_cli_optarg, &temp_log_level);
                ctx.LogLevel = (Aws::Crt::LogLevel)temp_log_level;
                if (ctx.LogLevel < Aws::Crt::LogLevel::Error)
                {
                    std::cerr << "unsupported log level " << aws_cli_optarg << std::endl;
                    s_usage(1);
                }
                break;
            }
            default:
                std::cerr << "Unknown option\n";
                s_usage(1);
        }
    }

    if (!ctx.uri)
    {
        std::cerr << "A URI for the request must be supplied.\n";
        s_usage(1);
    }
}

uint16_t receive_maximum = 9;
uint32_t maximum_packet_size = 128 * 1024;

/**********************************************************
 * MAIN
 **********************************************************/

/**
 * This is a sample to show basic functionality for the mqtt5 clients.
 * The app will demo connect/subscribe/publish/unsubscribe features, and
 * requires user interaction.
 * Please follow the instructions when [ACTION REQUIRED] pop up.
 *
 * The workflow for the application will be
 *  1. connect to server
 *  2. subscribe to topic "test/topic/test1", "test/topic/test2", and
 * "test/topic/test3"
 *  3. publish message "mqtt5 publish test"
 *  4. waiting for message from user for "test/topic/test1" and "test/topic/test2"
 *     to make sure the subscription succeed.
 *  5. unsubscribe from "test/topic/test1" and "test/topic/test2". Then make sure
 *     we are no longer subscribe to the topics.
 *  6. waiting for message from user for "test/topic/test3" to make sure we are still
 *     subscribing to "test/topic/test3"
 */

int main(int argc, char **argv)
{

    struct aws_allocator *allocator = aws_mem_tracer_new(aws_default_allocator(), NULL, AWS_MEMTRACE_STACKS, 15);

    struct app_ctx app_ctx = {};
    app_ctx.allocator = allocator;
    app_ctx.connect_timeout = 3000;
    app_ctx.port = 1883;

    s_parse_options(argc, argv, app_ctx);
    if (app_ctx.uri.GetPort())
    {
        app_ctx.port = app_ctx.uri.GetPort();
    }

    // s_aws_mqtt5_canary_update_tps_sleep_time(&tester_options);
    // s_aws_mqtt5_canary_init_weighted_operations(&tester_options);

    /**********************************************************
     * LOGGING
     **********************************************************/

    ApiHandle apiHandle(allocator);
    if (app_ctx.TraceFile)
    {
        apiHandle.InitializeLogging(app_ctx.LogLevel, app_ctx.TraceFile);
    }
    else
    {
        apiHandle.InitializeLogging(app_ctx.LogLevel, stderr);
    }

    bool useTls = false;

    auto hostName = app_ctx.uri.GetHostName();

    /***************************************************
     * setup connection configs
     ***************************************************/

    Io::TlsContextOptions tlsCtxOptions;
    Io::TlsContext tlsContext;
    Io::TlsConnectionOptions tlsConnectionOptions;
    if (useTls)
    {
        if (app_ctx.cert && app_ctx.key)
        {
            tlsCtxOptions = Io::TlsContextOptions::InitClientWithMtls(app_ctx.cert, app_ctx.key);
            if (!tlsCtxOptions)
            {
                std::cout << "Failed to load " << app_ctx.cert << " and " << app_ctx.key << " with error "
                          << aws_error_debug_str(tlsCtxOptions.LastError()) << std::endl;
                exit(1);
            }
        }
        else
        {
            tlsCtxOptions = Io::TlsContextOptions::InitDefaultClient();
            if (!tlsCtxOptions)
            {
                std::cout << "Failed to create a default tlsCtxOptions with error "
                          << aws_error_debug_str(tlsCtxOptions.LastError()) << std::endl;
                exit(1);
            }
        }

        tlsContext = Io::TlsContext(tlsCtxOptions, Io::TlsMode::CLIENT, allocator);

        tlsConnectionOptions = tlsContext.NewConnectionOptions();

        std::cout << "MQTT5: Looking into the uri string: "
                  << static_cast<const char *>(AWS_BYTE_CURSOR_PRI(app_ctx.uri.GetFullUri())) << std::endl;

        if (!tlsConnectionOptions.SetServerName(hostName))
        {
            std::cout << "Failed to set servername with error " << aws_error_debug_str(tlsConnectionOptions.LastError())
                      << std::endl;
            exit(1);
        }
        if (!tlsConnectionOptions.SetAlpnList("x-amzn-mqtt-ca"))
        {
            std::cout << "Failed to load alpn list with error " << aws_error_debug_str(tlsConnectionOptions.LastError())
                      << std::endl;
            exit(1);
        }
    }

    Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(app_ctx.connect_timeout);
    socketOptions.SetKeepAliveIntervalSec(0);
    socketOptions.SetKeepAlive(false);
    socketOptions.SetKeepAliveTimeoutSec(0);

    Io::EventLoopGroup eventLoopGroup(0, allocator);
    if (!eventLoopGroup)
    {
        std::cerr << "Failed to create evenloop group with error " << aws_error_debug_str(eventLoopGroup.LastError())
                  << std::endl;
        exit(1);
    }

    Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
    if (!defaultHostResolver)
    {
        std::cerr << "Failed to create host resolver with error "
                  << aws_error_debug_str(defaultHostResolver.LastError()) << std::endl;
        exit(1);
    }

    Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
    if (!clientBootstrap)
    {
        std::cerr << "Failed to create client bootstrap with error " << aws_error_debug_str(clientBootstrap.LastError())
                  << std::endl;
        exit(1);
    }
    clientBootstrap.EnableBlockingShutdown();

    /**********************************************************
     * MQTT5 CLIENT CREATION
     **********************************************************/
    std::cout << "**********************************************************" << std::endl;
    std::cout << "MQTT5: Start ConnectPacket...." << std::endl;
    std::cout << "**********************************************************" << std::endl;
    std::shared_ptr<Mqtt5::ConnectPacket> packet_connect = std::make_shared<Mqtt5::ConnectPacket>();
    packet_connect->WithReceiveMaximum(9);
    packet_connect->WithMaximumPacketSizeBytes(128 * 1024);

    std::cout << "**********************************************************" << std::endl;
    std::cout << "MQTT5: Start Option Builder...." << std::endl;
    std::cout << "**********************************************************" << std::endl;
    Aws::Crt::String namestring((const char *)hostName.ptr, hostName.len);
    Aws::Crt::Mqtt5::Mqtt5ClientOptions mqtt5OptionsBuilder(app_ctx.allocator);
    mqtt5OptionsBuilder.WithHostName(namestring).WithPort(app_ctx.port);

    mqtt5OptionsBuilder.WithConnectOptions(packet_connect)
        .WithSocketOptions(socketOptions)
        .WithBootstrap(&clientBootstrap);

    if (useTls)
    {
        mqtt5OptionsBuilder.WithTlsConnectionOptions(tlsConnectionOptions);
    }

    std::promise<bool> connectionPromise;
    std::promise<void> disconnectionPromise;
    std::promise<void> stoppedPromise;
    std::promise<void> publishReceivedPromise0;
    std::promise<void> publishReceivedPromise1;
    std::promise<void> publishReceivedPromise2;
    std::promise<void> publishReceivedPromise3;

    mqtt5OptionsBuilder.WithClientConnectionSuccessCallback(
        [&connectionPromise](const OnConnectionSuccessEventData &eventData)
        {
            std::cout << "**********************************************************" << std::endl;
            std::cout << "MQTT5:Connected:: " << eventData.negotiatedSettings->getClientId().c_str() << std::endl;
            std::cout << "**********************************************************" << std::endl;
            connectionPromise.set_value(true);
        });

    mqtt5OptionsBuilder.WithClientConnectionFailureCallback(
        [&connectionPromise](const OnConnectionFailureEventData &eventData)
        {
            std::cout << "**********************************************************" << std::endl;
            std::cout << "MQTT5:Connection failed with error " << aws_error_debug_str(eventData.errorCode) << std::endl;
            std::cout << "**********************************************************" << std::endl;
            connectionPromise.set_value(false);
        });

    mqtt5OptionsBuilder.WithClientStoppedCallback(
        [&stoppedPromise](const OnStoppedEventData &)
        {
            std::cout << "**********************************************************" << std::endl;
            std::cout << "MQTT5:client stopped." << std::endl;
            std::cout << "**********************************************************" << std::endl;
            stoppedPromise.set_value();
        });

    mqtt5OptionsBuilder.WithClientAttemptingConnectCallback(
        [](const OnAttemptingConnectEventData &) { std::cout << "MQTT5:client attempting connect." << std::endl; });

    mqtt5OptionsBuilder.WithClientDisconnectionCallback(
        [&disconnectionPromise](const OnDisconnectionEventData &eventData)
        {
            if (eventData.errorCode == 0)
            {
                std::cout << "**********************************************************" << std::endl;
                std::cout << "MQTT5:Diesconnected." << std::endl;
                std::cout << "**********************************************************" << std::endl;
            }
            else
            {
                std::cout << "**********************************************************" << std::endl;
                std::cout << "MQTT5:DisConnection failed with error " << aws_error_debug_str(eventData.errorCode) << std::endl;
                if (eventData.disconnectPacket != NULL)
                {
                    if (eventData.disconnectPacket->getReasonString().has_value())
                    {
                        std::cout << "disconnect packet: " << eventData.disconnectPacket->getReasonString().value().c_str()
                                  << std::endl;
                    }
                }
                std::cout << "**********************************************************" << std::endl;
            }
            disconnectionPromise.set_value();
        });

    mqtt5OptionsBuilder.WithPublishReceivedCallback(
        [&publishReceivedPromise1, &publishReceivedPromise2, &publishReceivedPromise3, &publishReceivedPromise0](
            const PublishReceivedEventData &eventData)
        {
            ByteCursor payload = eventData.publishPacket->getPayload();
            String msg = String((const char *)payload.ptr, payload.len);
            std::cout << "**********************************************************" << std::endl;
            for (Mqtt5::UserProperty prop : eventData.publishPacket->getUserProperties())
            {
                std::cout << "MQTT5:Received Message: "
                          << "UserProerty: " << prop.getName().c_str() << "," << prop.getValue().c_str() << std::endl;
            }
            std::cout << "MQTT5:Received Message: " << msg.c_str() << std::endl;
            std::cout << "**********************************************************" << std::endl;
            if (msg == "test1")
            {
                publishReceivedPromise1.set_value();
            }
            else if (msg == "test2")
            {
                publishReceivedPromise2.set_value();
            }
            else if (msg == "test3")
            {
                publishReceivedPromise3.set_value();
            }
            else if (msg == "mqtt5 publish test")
            {
                publishReceivedPromise0.set_value();
            }
        });

    std::cout << "**********************************************************" << std::endl;
    std::cout << "MQTT5: Start Init Client ...." << std::endl;
    auto mqtt5Client = Aws::Crt::Mqtt5::Mqtt5Client::NewMqtt5Client(mqtt5OptionsBuilder, app_ctx.allocator);

    if (mqtt5Client == nullptr)
    {
        std::cerr << "Failed to Init Mqtt5Client with error code: %d." << ErrorDebugString(LastError()) << std::endl;
        return -1;
    }

    std::cout << "MQTT5: Finish Init Client ...." << std::endl;
    std::cout << "**********************************************************" << std::endl;

    std::cout << "**********************************************************" << std::endl;
    std::cout << "MQTT5: Client Start ...." << std::endl;
    std::cout << "**********************************************************" << std::endl;

    if (mqtt5Client->Start() && connectionPromise.get_future().get() == true)
    {
        /**********************************************************
         * MQTT5 CLIENT SUBSCRIPTION
         **********************************************************/
        Mqtt5::Subscription data1(app_ctx.allocator);
        data1.WithNoLocal(false).WithTopicFilter("test/topic/test1").WithQOS(Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE);
        Mqtt5::Subscription data2(app_ctx.allocator);
        data2.WithTopicFilter("test/topic/test2").WithQOS(Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE);
        Mqtt5::Subscription data3(app_ctx.allocator);
        data3.WithTopicFilter("test/topic/test3").WithQOS(Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE);

        Vector<Mqtt5::Subscription> subscriptionList;
        subscriptionList.push_back(data1);
        subscriptionList.push_back(data2);
        subscriptionList.push_back(data3);

        std::shared_ptr<Mqtt5::SubscribePacket> subscribe = std::make_shared<Mqtt5::SubscribePacket>(app_ctx.allocator);
        subscribe->WithSubscriptions(subscriptionList);
        bool subscribeSuccess = mqtt5Client->Subscribe(
            subscribe,
            [](int, std::shared_ptr<Mqtt5::SubAckPacket> packet)
            {
                if(packet == nullptr) return;
                std::cout << "**********************************************************" << std::endl;
                std::cout << "MQTT5: check suback packet : " << std::endl;
                for (auto code : packet->getReasonCodes())
                {
                    std::cout << "Get suback with codes: " << code << std::endl;
                    if (code > Mqtt5::SubAckReasonCode::AWS_MQTT5_SARC_GRANTED_QOS_2)
                    {
                        std::cout << "Subscription Suceed." << std::endl;
                    }
                    else
                    {
                        std::cout << "Subscription Failed." << std::endl;
                    }
                }
                std::cout << "**********************************************************" << std::endl;
            });

        if (!subscribeSuccess)
        {
            std::cout << "[ERROR]Subscription Failed." << std::endl;
            if (mqtt5Client->Stop())
            {
                stoppedPromise.get_future().get();
            }
            else
            {
                std::cout << "[ERROR]Failed to stop the client " << std::endl;
            }
            return -1;
        }

        /**********************************************************
         * MQTT5 CLIENT PUBLISH
         **********************************************************/
        ByteCursor payload = Aws::Crt::ByteCursorFromCString("mqtt5 publish test");

        std::shared_ptr<Mqtt5::PublishPacket> publish = std::make_shared<Mqtt5::PublishPacket>(app_ctx.allocator);

        publish->WithTopic("test/topic/test1");
        publish->WithPayload(payload);
        publish->WithQOS(Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE);
        Mqtt5::UserProperty p1("propName1", "propValue1");
        Mqtt5::UserProperty p2("propName2", "propValue2");
        Mqtt5::UserProperty p3("propName3", "propValue3");
        Vector<Mqtt5::UserProperty> props;
        props.push_back(p1);
        props.push_back(p2);
        props.push_back(p3);
        Vector<Mqtt5::UserProperty> emptyprops;
        publish->WithUserProperties(props);
        publish->WithUserProperty(std::move(p1));
        publish->WithUserProperties(emptyprops); // test to reset the user properties
        publish->WithResponseTopic(ByteCursorFromCString("test/*"));

        std::cout << "**********************************************************" << std::endl;
        std::cout << "Publish Start:" << std::endl;
        std::cout << "**********************************************************" << std::endl;
        if (!mqtt5Client->Publish(publish))
        {
            std::cout << "**********************************************************" << std::endl;
            std::cout << "[ERROR]Publish Failed." << std::endl;
            std::cout << "**********************************************************" << std::endl;
            mqtt5Client->Stop();
            stoppedPromise.get_future().get();
            return -1;
        }

        std::cout << "**********************************************************" << std::endl;
        std::cout << "Mqtt5: Waiting for published message..." << std::endl;
        std::cout << "**********************************************************" << std::endl;
        publishReceivedPromise0.get_future().get();
        std::cout << "**********************************************************" << std::endl;
        std::cout << "[Action Required]Please publish a message \"test1\" to topic \"test/topic/test1\". And make sure "
                     "we recieved message."
                  << std::endl;
        std::cout << "**********************************************************" << std::endl;
        publishReceivedPromise1.get_future().get();
        std::cout << "**********************************************************" << std::endl;
        std::cout << "[Action Required]Please publish a message \"test2\" to topic \"test/topic/test2\". And make sure "
                     "we recieved message."
                  << std::endl;
        std::cout << "**********************************************************" << std::endl;
        publishReceivedPromise2.get_future().get();

        /**********************************************************
         * MQTT5 CLIENT UNSUBSCRIBE
         **********************************************************/
        String topic1 = "test/topic/test1";
        String topic2 = "test/topic/test2";
        Vector<String> topics;
        topics.push_back(topic1);
        topics.push_back(topic2);
        std::shared_ptr<Mqtt5::UnsubscribePacket> unsub = std::make_shared<Mqtt5::UnsubscribePacket>(app_ctx.allocator);
        unsub->WithTopicFilters(topics);
        if (!mqtt5Client->Unsubscribe(unsub))
        {
            std::cout << "[ERROR]Unsubscribe Failed." << std::endl;
            mqtt5Client->Stop();
            stoppedPromise.get_future().get();
            return -1;
        }

        std::cout << "**********************************************************" << std::endl;
        std::cout << "Unsubscription Succeed. Now we are no longer subscribe to \"test/topic/test1\" and "
                     "\"test/topic/test2\"."
                  << std::endl;
        std::cout << "[Action Required]Please publish a message to topic \"test/topic/test1\" or \"test/topic/test2\". "
                     "And make sure we do not recieve any message."
                  << std::endl;
        std::cout << "Then please publish a message to topic \"test/topic/test3\" to make sure we didn't unsubscribe "
                     "from \"test/topic/test3\"."
                  << std::endl;
        std::cout << "**********************************************************" << std::endl;

        publishReceivedPromise3.get_future().get();
        Mqtt5::DisconnectPacket disconnect(app_ctx.allocator);
        disconnect.WithReasonString("disconnect test string");
        if (mqtt5Client->Stop())
        {
            stoppedPromise.get_future().get();
        }
        else
        {
            std::cout << "[ERROR]Failed to stop the client " << std::endl;
        }
    }
    else
    {
        std::cout << "[ERROR]Failed to start the client " << std::endl;
    }
    return 0;
}
