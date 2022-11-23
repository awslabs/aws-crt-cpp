/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Api.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/crypto/Hash.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/TlsOptions.h>
#include <aws/crt/io/Uri.h>

#include <aws/crt/mqtt/Mqtt5Client.h>
#include <aws/crt/mqtt/Mqtt5Packets.h>

#include <aws/common/clock.h>
#include <aws/common/command_line_parser.h>
#include <aws/common/mutex.h>
#include <condition_variable>
#include <inttypes.h>
#include <iostream>

#define AWS_MQTT5_CANARY_CLIENT_CREATION_SLEEP_TIME 10000000
#define AWS_MQTT5_CANARY_OPERATION_ARRAY_SIZE 10000
#define AWS_MQTT5_CANARY_TOPIC_ARRAY_SIZE 256
#define AWS_MQTT5_CANARY_CLIENT_MAX 50
#define AWS_MQTT5_CANARY_PAYLOAD_SIZE_MAX UINT16_MAX

using namespace Aws::Crt;

struct AppCtx
{
    Allocator *allocator;
    struct aws_mutex lock;
    Io::Uri uri;
    uint16_t port;
    const char *cacert;
    const char *cert;
    const char *key;
    int connect_timeout;
    bool use_websockets;
    bool use_tls;

    Io::TlsConnectionOptions tls_connection_options;

    const char *TraceFile;
    Aws::Crt::LogLevel LogLevel;
};

enum AwsMqtt5CanaryOperations
{
    AWS_MQTT5_CANARY_OPERATION_NULL = 0,
    AWS_MQTT5_CANARY_OPERATION_START = 1,
    AWS_MQTT5_CANARY_OPERATION_STOP = 2,
    AWS_MQTT5_CANARY_OPERATION_DESTROY = 3,
    AWS_MQTT5_CANARY_OPERATION_SUBSCRIBE = 4,
    AWS_MQTT5_CANARY_OPERATION_UNSUBSCRIBE = 5,
    AWS_MQTT5_CANARY_OPERATION_UNSUBSCRIBE_BAD = 6,
    AWS_MQTT5_CANARY_OPERATION_PUBLISH_QOS0 = 7,
    AWS_MQTT5_CANARY_OPERATION_PUBLISH_QOS1 = 8,
    AWS_MQTT5_CANARY_OPERATION_PUBLISH_TO_SUBSCRIBED_TOPIC_QOS0 = 9,
    AWS_MQTT5_CANARY_OPERATION_PUBLISH_TO_SUBSCRIBED_TOPIC_QOS1 = 10,
    AWS_MQTT5_CANARY_OPERATION_PUBLISH_TO_SHARED_TOPIC_QOS0 = 11,
    AWS_MQTT5_CANARY_OPERATION_PUBLISH_TO_SHARED_TOPIC_QOS1 = 12,
    AWS_MQTT5_CANARY_OPERATION_COUNT = 13,
};

struct AwsMqtt5CanaryTesterOptions
{
    uint16_t elgMaxThreads;
    uint16_t clientCount;
    size_t tps;
    uint64_t tpsSleepTime;
    size_t distributionsTotal;
    enum AwsMqtt5CanaryOperations *operations;
    size_t testRunSeconds;
    size_t memoryCheckIntervalSec; // Print memory usage every monitorSecond
};

static void s_Usage(int exit_code)
{

    fprintf(stderr, "usage: mqtt5_canary [options] endpoint\n");
    fprintf(stderr, " endpoint: url to connect to\n");
    fprintf(stderr, "\n Options:\n\n");
    fprintf(stderr, "      --cacert FILE: path to a CA certficate file.\n");
    fprintf(stderr, "      --cert FILE: path to a PEM encoded certificate to use with mTLS\n");
    fprintf(stderr, "      --key FILE: Path to a PEM encoded private key that matches cert.\n");
    fprintf(stderr, "      --connect-timeout INT: time in milliseconds to wait for a connection.\n");
    fprintf(stderr, "  -l, --log FILE: dumps logs to FILE instead of stderr.\n");
    fprintf(stderr, "  -v, --verbose: ERROR|INFO|DEBUG|TRACE: log level to configure. Default is none.\n");
    fprintf(stderr, "  -w, --websockets: use mqtt-over-websockets rather than direct mqtt\n");
    fprintf(stderr, "  -u, --tls: use tls with mqtt connection\n");

    fprintf(stderr, "  -t, --threads: number of eventloop group threads to use\n");
    fprintf(stderr, "  -C, --clients: number of mqtt5 clients to use\n");
    fprintf(stderr, "  -T, --tps: operations to run per second\n");
    fprintf(stderr, "  -s, --seconds: seconds to run canary test\n");
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
    {"websockets", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'w'},
    {"help", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'h'},

    {"threads", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 't'},
    {"clients", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'C'},
    {"tps", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'T'},
    {"seconds", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 's'},
    /* Per getopt(3) the last element of the array has to be filled with all zeros */
    {NULL, AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 0},
};

static void s_ParseOptions(int argc, char **argv, struct AppCtx &ctx, struct AwsMqtt5CanaryTesterOptions *testerOptions)
{

    while (true)
    {
        int option_index = 0;
        int c = aws_cli_getopt_long(argc, argv, "a:c:e:f:l:v:wht:C:T:s:", s_long_options, &option_index);
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
            case 'a':
                ctx.cacert = aws_cli_optarg;
                break;
            case 'c':
                ctx.cert = aws_cli_optarg;
                break;
            case 'e':
                ctx.key = aws_cli_optarg;
                break;
            case 'f':
                ctx.connect_timeout = atoi(aws_cli_optarg);
                break;
            case 'l':
                ctx.TraceFile = aws_cli_optarg;
                break;
            case 'h':
                s_Usage(0);
                break;
            case 'w':
                ctx.use_websockets = true;
                break;
            case 'u':
                ctx.use_tls = true;
            case 't':
                ctx.TraceFile = aws_cli_optarg;
                break;
            case 'v':
                if (!strcmp(aws_cli_optarg, "TRACE"))
                {
                    ctx.LogLevel = Aws::Crt::LogLevel::Trace;
                }
                else if (!strcmp(aws_cli_optarg, "INFO"))
                {
                    ctx.LogLevel = Aws::Crt::LogLevel::Info;
                }
                else if (!strcmp(aws_cli_optarg, "DEBUG"))
                {
                    ctx.LogLevel = Aws::Crt::LogLevel::Debug;
                }
                else if (!strcmp(aws_cli_optarg, "ERROR"))
                {
                    ctx.LogLevel = Aws::Crt::LogLevel::Error;
                }
                else
                {
                    std::cerr << "unsupported log level " << aws_cli_optarg << std::endl;
                    s_Usage(1);
                }
                break;
            case 'C':
                testerOptions->clientCount = static_cast<uint16_t>(atoi(aws_cli_optarg));
                if (testerOptions->clientCount > AWS_MQTT5_CANARY_CLIENT_MAX)
                {
                    testerOptions->clientCount = AWS_MQTT5_CANARY_CLIENT_MAX;
                }
                break;
            case 'T':
                testerOptions->tps = static_cast<uint16_t>(atoi(aws_cli_optarg));
                break;
            case 's':
                testerOptions->testRunSeconds = atoi(aws_cli_optarg);
                break;
            case 0x02:
                /* getopt_long() returns 0x02 (START_OF_TEXT) if a positional arg was encountered */
                ctx.uri = Io::Uri(aws_byte_cursor_from_c_str(aws_cli_positional_arg), ctx.allocator);
                if (!ctx.uri)
                {
                    std::cerr << "Failed to parse uri \"" << aws_cli_positional_arg << "\" with error "
                              << aws_error_debug_str(ctx.uri.LastError()) << std::endl;
                    s_Usage(1);
                }
                else
                {
                    std::cerr << "Success to parse uri \"" << aws_cli_positional_arg
                              << static_cast<const char *>(AWS_BYTE_CURSOR_PRI(ctx.uri.GetFullUri())) << std::endl;
                }
                break;
            default:
                std::cerr << "Unknown option\n";
                s_Usage(1);
        }
    }

    if (!ctx.uri)
    {
        std::cerr << "A URI for the request must be supplied.\n";
        s_Usage(1);
    }
}

/**********************************************************
 * MQTT5 CANARY OPTIONS
 **********************************************************/

static void s_Mqtt5CanaryUpdateTpsSleepTime(struct AwsMqtt5CanaryTesterOptions *testerOptions)
{
    testerOptions->tpsSleepTime =
        (aws_timestamp_convert(1, AWS_TIMESTAMP_SECS, AWS_TIMESTAMP_NANOS, NULL) / testerOptions->tps);
}

static void s_AwsMqtt5CanaryInitTesterOptions(struct AwsMqtt5CanaryTesterOptions *testerOptions)
{
    /* number of eventloop group threads to use */
    testerOptions->elgMaxThreads = 3;
    /* number of mqtt5 clients to use */
    testerOptions->clientCount = 10;
    /* operations per second to run */
    testerOptions->tps = 50;
    /* How long to run the test before exiting */
    testerOptions->testRunSeconds = 60;
    /* Time interval for printing memory usage info */
    testerOptions->memoryCheckIntervalSec = 15;
}

struct AwsMqtt5CanaryTestClient
{
    std::shared_ptr<Mqtt5::Mqtt5Client> client;
    std::shared_ptr<Mqtt5::NegotiatedSettings> settings;
    Aws::Crt::String sharedTopic;
    Aws::Crt::String clientId;
    size_t subscriptionCount;
    bool isConnected;

    ~AwsMqtt5CanaryTestClient()
    {
        if (client != nullptr)
        {
            client.reset();
        }
        if (settings != nullptr)
        {
            settings.reset();
        }
    }
};

/**********************************************************
 * OPERATION DISTRIBUTION
 **********************************************************/

typedef int(awsMqtt5CanaryOperationFn)(AwsMqtt5CanaryTestClient *testClient, Allocator *allocator);

struct AwsMqtt5CanaryOperationsFunctionTable
{
    awsMqtt5CanaryOperationFn *operationByOperationType[AWS_MQTT5_CANARY_OPERATION_COUNT];
};

static void s_AwsMqtt5CanaryAddOperationToArray(
    AwsMqtt5CanaryTesterOptions *tester_options,
    AwsMqtt5CanaryOperations operation_type,
    size_t probability)
{
    for (size_t i = 0; i < probability; ++i)
    {

        tester_options->operations[tester_options->distributionsTotal] = operation_type;
        tester_options->distributionsTotal += 1;
    }
}

/* Add operations and their weighted probability to the list of possible operations */
static void s_AwsMqtt5CanaryInitWeightedOperations(AwsMqtt5CanaryTesterOptions *testerOptions)
{

    s_AwsMqtt5CanaryAddOperationToArray(testerOptions, AWS_MQTT5_CANARY_OPERATION_STOP, 1);
    s_AwsMqtt5CanaryAddOperationToArray(testerOptions, AWS_MQTT5_CANARY_OPERATION_SUBSCRIBE, 200);
    s_AwsMqtt5CanaryAddOperationToArray(testerOptions, AWS_MQTT5_CANARY_OPERATION_UNSUBSCRIBE, 200);
    s_AwsMqtt5CanaryAddOperationToArray(testerOptions, AWS_MQTT5_CANARY_OPERATION_UNSUBSCRIBE_BAD, 100);
    s_AwsMqtt5CanaryAddOperationToArray(testerOptions, AWS_MQTT5_CANARY_OPERATION_PUBLISH_QOS0, 300);
    s_AwsMqtt5CanaryAddOperationToArray(testerOptions, AWS_MQTT5_CANARY_OPERATION_PUBLISH_QOS1, 150);
    s_AwsMqtt5CanaryAddOperationToArray(
        testerOptions, AWS_MQTT5_CANARY_OPERATION_PUBLISH_TO_SUBSCRIBED_TOPIC_QOS0, 100);
    s_AwsMqtt5CanaryAddOperationToArray(testerOptions, AWS_MQTT5_CANARY_OPERATION_PUBLISH_TO_SUBSCRIBED_TOPIC_QOS1, 50);
    s_AwsMqtt5CanaryAddOperationToArray(testerOptions, AWS_MQTT5_CANARY_OPERATION_PUBLISH_TO_SHARED_TOPIC_QOS0, 50);
    s_AwsMqtt5CanaryAddOperationToArray(testerOptions, AWS_MQTT5_CANARY_OPERATION_PUBLISH_TO_SHARED_TOPIC_QOS1, 50);
}

static AwsMqtt5CanaryOperations s_AwsMqtt5CanaryGetRandomOperation(AwsMqtt5CanaryTesterOptions *testerOptions)
{
    size_t random_index = rand() % testerOptions->distributionsTotal;

    return testerOptions->operations[random_index];
}

/**********************************************************
 * CLIENT OPTIONS
 **********************************************************/

static void s_AwsMqtt5TransformWebsocketHandshakeFn(
    std::shared_ptr<Http::HttpRequest> req1,
    const Mqtt5::OnWebSocketHandshakeInterceptComplete &onComplete)
{
    onComplete(req1, AWS_ERROR_SUCCESS);
}

/**********************************************************
 * OPERATION FUNCTIONS
 **********************************************************/

static int s_AwsMqtt5CanaryOperationStart(struct AwsMqtt5CanaryTestClient *testClient, Allocator * /*allocator*/)
{
    if (testClient->isConnected)
    {
        return AWS_OP_SUCCESS;
    }

    if (testClient->client == nullptr)
    {
        AWS_LOGF_INFO(AWS_LS_MQTT5_CANARY, "Invalid Client, Client Creation Failed.");
        return AWS_OP_ERR;
    }

    if (testClient->client->Start())
    {
        if (!testClient->clientId.empty())
        {
            AWS_LOGF_INFO(AWS_LS_MQTT5_CANARY, "ID:%s Start", testClient->clientId.c_str());
        }
        else
        {
            testClient->clientId = Aws::Crt::String("Client ID not set");
        }
        return AWS_OP_SUCCESS;
    }
    return AWS_OP_ERR;
}

static int s_AwsMqtt5CanaryOperationStop(struct AwsMqtt5CanaryTestClient *testClient, Allocator * /*allocator*/)
{
    if (!testClient->isConnected)
    {
        return AWS_OP_SUCCESS;
    }
    if (testClient->client->Stop())
    {
        testClient->subscriptionCount = 0;
        AWS_LOGF_INFO(AWS_LS_MQTT5_CANARY, "ID:%s Stop", testClient->clientId.c_str());
        return AWS_OP_SUCCESS;
    }
    return AWS_OP_ERR;
}

static int s_AwsMqtt5CanaryOperationSubscribe(struct AwsMqtt5CanaryTestClient *testClient, Allocator *allocator)
{
    if (!testClient->isConnected)
    {
        return s_AwsMqtt5CanaryOperationStart(testClient, allocator);
    }
    char topicArray[AWS_MQTT5_CANARY_TOPIC_ARRAY_SIZE];
    AWS_ZERO_STRUCT(topicArray);
    snprintf(topicArray, sizeof topicArray, "%s_%zu", testClient->clientId.c_str(), testClient->subscriptionCount);

    Mqtt5::Subscription subscription1;
    subscription1.withTopicFilter(Aws::Crt::String(topicArray))
        .withNoLocal(false)
        .withQOS(Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE)
        .withRetainHandlingType(Mqtt5::RetainHandlingType::AWS_MQTT5_RHT_SEND_ON_SUBSCRIBE)
        .withRetain(false);

    Mqtt5::Subscription subscription2;
    subscription2.withTopicFilter(testClient->sharedTopic)
        .withNoLocal(false)
        .withQOS(Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE)
        .withRetainHandlingType(Mqtt5::RetainHandlingType::AWS_MQTT5_RHT_SEND_ON_SUBSCRIBE)
        .withRetain(false);

    std::shared_ptr<Mqtt5::SubscribePacket> packet = std::make_shared<Mqtt5::SubscribePacket>(allocator);
    packet->withSubscription(std::move(subscription1));
    packet->withSubscription(std::move(subscription2));

    testClient->subscriptionCount++;

    AWS_LOGF_INFO(AWS_LS_MQTT5_CANARY, "ID:%s Subscribe to topic: %s", testClient->clientId.c_str(), topicArray);

    if (testClient->client->Subscribe(packet))
    {
        return AWS_OP_SUCCESS;
    }
    return AWS_OP_ERR;
}

static int s_AwsMqtt5CanaryOperationUnsubscribeBad(struct AwsMqtt5CanaryTestClient *testClient, Allocator *allocator)
{
    if (!testClient->isConnected)
    {
        return s_AwsMqtt5CanaryOperationStart(testClient, allocator);
    }
    char topicArray[AWS_MQTT5_CANARY_TOPIC_ARRAY_SIZE];
    AWS_ZERO_STRUCT(topicArray);
    snprintf(topicArray, sizeof topicArray, "%s_non_existing_topic", testClient->clientId.c_str());

    Vector<Aws::Crt::String> topics;
    topics.push_back(Aws::Crt::String(topicArray));

    std::shared_ptr<Mqtt5::UnsubscribePacket> unsubscription = std::make_shared<Mqtt5::UnsubscribePacket>(allocator);
    unsubscription->withTopicFilters(topics);

    if (testClient->client->Unsubscribe(unsubscription))
    {
        AWS_LOGF_INFO(AWS_LS_MQTT5_CANARY, "ID:%s Unsubscribe Bad", testClient->clientId.c_str());
        return AWS_OP_SUCCESS;
    }
    return AWS_OP_ERR;
}

static int s_AwsMqtt5CanaryOperationUnsubscribe(struct AwsMqtt5CanaryTestClient *testClient, Allocator *allocator)
{
    if (!testClient->isConnected)
    {
        return s_AwsMqtt5CanaryOperationStart(testClient, allocator);
    }

    if (testClient->subscriptionCount <= 0)
    {
        return s_AwsMqtt5CanaryOperationUnsubscribeBad(testClient, allocator);
    }

    testClient->subscriptionCount--;
    char topicArray[AWS_MQTT5_CANARY_TOPIC_ARRAY_SIZE];
    AWS_ZERO_STRUCT(topicArray);
    snprintf(topicArray, sizeof topicArray, "%s_%zu", testClient->clientId.c_str(), testClient->subscriptionCount);

    Vector<Aws::Crt::String> topics;
    topics.push_back(Aws::Crt::String(topicArray));

    std::shared_ptr<Mqtt5::UnsubscribePacket> unsubscription = std::make_shared<Mqtt5::UnsubscribePacket>(allocator);
    unsubscription->withTopicFilters(topics);

    if (testClient->client->Unsubscribe(unsubscription))
    {
        AWS_LOGF_INFO(
            AWS_LS_MQTT5_CANARY, "ID:%s Unsubscribe from topic: %s", testClient->clientId.c_str(), topicArray);
        return AWS_OP_SUCCESS;
    }
    return AWS_OP_ERR;
}

/* Help function for Publish Operation. Do not call it directly for operations. */
static int s_AwsMqtt5CanaryOperationPublish(
    struct AwsMqtt5CanaryTestClient *testClient,
    Aws::Crt::String topicFilter,
    Mqtt5::QOS qos,
    Allocator *allocator)
{
    Mqtt5::UserProperty up1("property1", "value1");
    Mqtt5::UserProperty up2("property2", "value2");
    Mqtt5::UserProperty up3("property3", "value3");

    uint16_t payload_size = (rand() % UINT16_MAX) + 1;
    uint8_t payload_data[AWS_MQTT5_CANARY_PAYLOAD_SIZE_MAX];
    ByteCursor payload = ByteCursorFromArray(payload_data, payload_size);

    std::shared_ptr<Mqtt5::PublishPacket> packetPublish = std::make_shared<Mqtt5::PublishPacket>(allocator);
    packetPublish->withTopic(topicFilter)
        .withQOS(qos)
        .withRetain(false)
        .withPayload(payload)
        .withUserProperty(std::move(up1))
        .withUserProperty(std::move(up2))
        .withUserProperty(std::move(up3));

    if (testClient->client->Publish(packetPublish))
    {
        return AWS_OP_SUCCESS;
    }
    return AWS_OP_ERR;
}

static int s_AwsMqtt5CanaryOperationPublishQos0(struct AwsMqtt5CanaryTestClient *testClient, Allocator *allocator)
{
    if (!testClient->isConnected)
    {
        return s_AwsMqtt5CanaryOperationStart(testClient, allocator);
    }

    Aws::Crt::String topic = "topic1";
    AWS_LOGF_INFO(AWS_LS_MQTT5_CANARY, "ID:%s Publish qos0", testClient->clientId.c_str());
    return s_AwsMqtt5CanaryOperationPublish(testClient, topic, AWS_MQTT5_QOS_AT_MOST_ONCE, allocator);
}

static int s_AwsMqtt5CanaryOperationPublishQos1(struct AwsMqtt5CanaryTestClient *testClient, Allocator *allocator)
{
    if (!testClient->isConnected)
    {
        return s_AwsMqtt5CanaryOperationStart(testClient, allocator);
    }
    Aws::Crt::String topic = "topic1";
    AWS_LOGF_INFO(AWS_LS_MQTT5_CANARY, "ID:%s Publish qos1", testClient->clientId.c_str());
    return s_AwsMqtt5CanaryOperationPublish(testClient, topic, AWS_MQTT5_QOS_AT_LEAST_ONCE, allocator);
}

static int s_AwsMqtt5CanaryOperationPublishToSubscribedTopicQos0(
    struct AwsMqtt5CanaryTestClient *testClient,
    Allocator *allocator)
{
    if (!testClient->isConnected)
    {
        return s_AwsMqtt5CanaryOperationStart(testClient, allocator);
    }

    if (testClient->subscriptionCount < 1)
    {
        return s_AwsMqtt5CanaryOperationPublishQos0(testClient, allocator);
    }
    char topicArray[AWS_MQTT5_CANARY_TOPIC_ARRAY_SIZE];
    AWS_ZERO_STRUCT(topicArray);
    snprintf(topicArray, sizeof topicArray, "%s_%zu", testClient->clientId.c_str(), testClient->subscriptionCount - 1);

    AWS_LOGF_INFO(
        AWS_LS_MQTT5_CANARY, "ID:%s Publish qos 0 to subscribed topic: %s", testClient->clientId.c_str(), topicArray);
    return s_AwsMqtt5CanaryOperationPublish(testClient, topicArray, AWS_MQTT5_QOS_AT_MOST_ONCE, allocator);
}

static int s_AwsMqtt5CanaryOperationPublishToSubscribedTopicQos1(
    struct AwsMqtt5CanaryTestClient *testClient,
    Allocator *allocator)
{
    if (!testClient->isConnected)
    {
        return s_AwsMqtt5CanaryOperationStart(testClient, allocator);
    }

    if (testClient->subscriptionCount < 1)
    {
        return s_AwsMqtt5CanaryOperationPublishQos1(testClient, allocator);
    }

    char topicArray[AWS_MQTT5_CANARY_TOPIC_ARRAY_SIZE];
    AWS_ZERO_STRUCT(topicArray);
    snprintf(topicArray, sizeof topicArray, "%s_%zu", testClient->clientId.c_str(), testClient->subscriptionCount - 1);

    AWS_LOGF_INFO(
        AWS_LS_MQTT5_CANARY, "ID:%s Publish qos 1 to subscribed topic: %s", testClient->clientId.c_str(), topicArray);
    return s_AwsMqtt5CanaryOperationPublish(testClient, topicArray, AWS_MQTT5_QOS_AT_LEAST_ONCE, allocator);
}

static int s_AwsMqtt5CanaryOperationPublishToSharedTopicQos0(
    struct AwsMqtt5CanaryTestClient *testClient,
    Allocator *allocator)
{
    if (!testClient->isConnected)
    {
        return s_AwsMqtt5CanaryOperationStart(testClient, allocator);
    }
    AWS_LOGF_INFO(
        AWS_LS_MQTT5_CANARY,
        "ID:%s Publish qos 0 to shared topic: %s",
        testClient->clientId.c_str(),
        testClient->sharedTopic.c_str());
    return s_AwsMqtt5CanaryOperationPublish(testClient, testClient->sharedTopic, AWS_MQTT5_QOS_AT_MOST_ONCE, allocator);
}

static int s_AwsMqtt5CanaryOperationPublishToSharedTopicQos1(
    struct AwsMqtt5CanaryTestClient *testClient,
    Allocator *allocator)
{
    if (!testClient->isConnected)
    {
        return s_AwsMqtt5CanaryOperationStart(testClient, allocator);
    }
    AWS_LOGF_INFO(
        AWS_LS_MQTT5_CANARY,
        "ID:%s Publish qos 1 to shared topic: %s",
        testClient->clientId.c_str(),
        testClient->sharedTopic.c_str());
    return s_AwsMqtt5CanaryOperationPublish(
        testClient, testClient->sharedTopic, AWS_MQTT5_QOS_AT_LEAST_ONCE, allocator);
}

static struct AwsMqtt5CanaryOperationsFunctionTable s_AwsMqtt5CanaryOperationTable = {
        {
            NULL,                                                   /* null */
            &s_AwsMqtt5CanaryOperationStart,                        /* start */
            &s_AwsMqtt5CanaryOperationStop,                         /* stop */
            NULL,                                                   /* destroy */
            &s_AwsMqtt5CanaryOperationSubscribe,                    /* subscribe */
            &s_AwsMqtt5CanaryOperationUnsubscribe,                  /* unsubscribe */
            &s_AwsMqtt5CanaryOperationUnsubscribeBad,               /* unsubscribe_bad */
            &s_AwsMqtt5CanaryOperationPublishQos0,                  /* publish_qos0 */
            &s_AwsMqtt5CanaryOperationPublishQos1,                  /* publish_qos1 */
            &s_AwsMqtt5CanaryOperationPublishToSubscribedTopicQos0, /* publish_to_subscribed_topic_qos0 */
            &s_AwsMqtt5CanaryOperationPublishToSubscribedTopicQos1, /* publish_to_subscribed_topic_qos1 */
            &s_AwsMqtt5CanaryOperationPublishToSharedTopicQos0,     /* publish_to_shared_topic_qos0 */
            &s_AwsMqtt5CanaryOperationPublishToSharedTopicQos1,     /* publish_to_shared_topic_qos1 */
        }
};

/**********************************************************
 * MAIN
 **********************************************************/

int main(int argc, char **argv)
{
    struct aws_allocator *allocator = aws_mem_tracer_new(aws_default_allocator(), NULL, AWS_MEMTRACE_STACKS, 15);

    ApiHandle apiHandle(allocator);
    struct AppCtx appCtx;
    AWS_ZERO_STRUCT(appCtx);
    appCtx.allocator = allocator;
    appCtx.connect_timeout = 3000;
    aws_mutex_init(&appCtx.lock);
    appCtx.port = 1883;

    struct AwsMqtt5CanaryTesterOptions testerOptions;
    AWS_ZERO_STRUCT(testerOptions);
    s_AwsMqtt5CanaryInitTesterOptions(&testerOptions);
    enum AwsMqtt5CanaryOperations operations[AWS_MQTT5_CANARY_OPERATION_ARRAY_SIZE];
    AWS_ZERO_STRUCT(operations);
    testerOptions.operations = operations;

    s_ParseOptions(argc, argv, appCtx, &testerOptions);
    if (appCtx.uri.GetPort())
    {
        appCtx.port = appCtx.uri.GetPort();
    }

    s_Mqtt5CanaryUpdateTpsSleepTime(&testerOptions);
    s_AwsMqtt5CanaryInitWeightedOperations(&testerOptions);

    /**********************************************************
     * LOGGING
     **********************************************************/

    // The log options is used to log memory allocation info after API get released.
    struct aws_logger_standard_options logOptions;
    logOptions.level= (aws_log_level)appCtx.LogLevel;

    if (appCtx.TraceFile)
    {
        apiHandle.InitializeLogging(appCtx.LogLevel, appCtx.TraceFile);
        logOptions.filename = appCtx.TraceFile;
    }
    else
    {
        apiHandle.InitializeLogging(appCtx.LogLevel, stderr);
        logOptions.file = stderr;
    }

    /***************************************************
     * TLS
     ***************************************************/
    auto hostName = appCtx.uri.GetHostName();
    Io::TlsContextOptions tlsCtxOptions;
    Io::TlsContext tlsContext;
    Io::TlsConnectionOptions tlsConnectionOptions;
    if (appCtx.use_tls)
    {
        if (appCtx.cert && appCtx.key)
        {
            tlsCtxOptions = Io::TlsContextOptions::InitClientWithMtls(appCtx.cert, appCtx.key);
            if (!tlsCtxOptions)
            {
                std::cout << "Failed to load " << appCtx.cert << " and " << appCtx.key << " with error "
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

        tlsContext = Io::TlsContext(tlsCtxOptions, Io::TlsMode::CLIENT, appCtx.allocator);

        tlsConnectionOptions = tlsContext.NewConnectionOptions();

        std::cout << "[MQTT5] Looking into the uri string: "
                  << static_cast<const char *>(AWS_BYTE_CURSOR_PRI(appCtx.uri.GetFullUri())) << std::endl;

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

    /**********************************************************
     * EVENT LOOP GROUP
     **********************************************************/

    Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(appCtx.connect_timeout);
    socketOptions.SetKeepAliveIntervalSec(10000);

    Io::EventLoopGroup eventLoopGroup(testerOptions.elgMaxThreads, appCtx.allocator);
    if (!eventLoopGroup)
    {
        std::cerr << "Failed to create evenloop group with error " << aws_error_debug_str(eventLoopGroup.LastError())
                  << std::endl;
        exit(1);
    }

    Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, appCtx.allocator);
    if (!defaultHostResolver)
    {
        std::cerr << "Failed to create host resolver with error "
                  << aws_error_debug_str(defaultHostResolver.LastError()) << std::endl;
        exit(1);
    }

    Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, appCtx.allocator);
    if (!clientBootstrap)
    {
        std::cerr << "Failed to create client bootstrap with error " << aws_error_debug_str(clientBootstrap.LastError())
                  << std::endl;
        exit(1);
    }

    /**********************************************************
     * MQTT5 CLIENT CREATION
     **********************************************************/

    uint16_t receive_maximum = 9;
    uint32_t maximum_packet_size = 128 * 1024;

    std::shared_ptr<Mqtt5::ConnectPacket> packetConnect = std::make_shared<Mqtt5::ConnectPacket>(allocator);
    packetConnect->withKeepAliveIntervalSec(30)
        .withMaximumPacketSizeBytes(maximum_packet_size)
        .withReceiveMaximum(receive_maximum);

    Aws::Crt::String namestring((const char *)hostName.ptr, hostName.len);
    Aws::Crt::Mqtt5::Mqtt5ClientOptions mqtt5Options(appCtx.allocator);
    mqtt5Options.withHostName(namestring)
        .withPort(appCtx.port)
        .withConnectOptions(packetConnect)
        .withSocketOptions(socketOptions)
        .withBootstrap(&clientBootstrap)
        .withPingTimeoutMs(10000)
        .withReconnectOptions({AWS_EXPONENTIAL_BACKOFF_JITTER_NONE, 1000, 120000, 3000});

    if (appCtx.use_tls)
    {
        mqtt5Options.withTlsConnectionOptions(tlsConnectionOptions);
    }

    if (appCtx.use_websockets)
    {
        mqtt5Options.withWebsocketHandshakeTransformCallback(s_AwsMqtt5TransformWebsocketHandshakeFn);
    }

    std::vector<struct AwsMqtt5CanaryTestClient> clients;

    uint64_t startTime = 0;
    aws_high_res_clock_get_ticks(&startTime);
    char sharedTopicArray[AWS_MQTT5_CANARY_TOPIC_ARRAY_SIZE];
    AWS_ZERO_STRUCT(sharedTopicArray);
    snprintf(sharedTopicArray, sizeof sharedTopicArray, "%" PRId64 "_shared_topic", startTime);

    for (size_t i = 0; i < testerOptions.clientCount; ++i)
    {
        struct AwsMqtt5CanaryTestClient client;
        AWS_ZERO_STRUCT(client);
        client.sharedTopic = Aws::Crt::String(sharedTopicArray);
        mqtt5Options.withPublishReceivedCallback(
            [&client](std::shared_ptr<Mqtt5::Mqtt5Client> /*client*/, std::shared_ptr<Mqtt5::PublishPacket> publish) {
                if (publish == nullptr)
                {
                    AWS_LOGF_INFO(
                        AWS_LS_MQTT5_CANARY, "Client:[%s] NULL Publish Packet Received.", client.clientId.c_str());
                    return;
                }
                AWS_LOGF_INFO(
                    AWS_LS_MQTT5_CANARY,
                    "Client:%s Publish Received on topic %s",
                    client.clientId.c_str(),
                    publish->getTopic().c_str());
            });

        mqtt5Options.withClientConnectionSuccessCallback(
            [&client](
                Mqtt5::Mqtt5Client &,
                std::shared_ptr<Aws::Crt::Mqtt5::ConnAckPacket>,
                std::shared_ptr<Aws::Crt::Mqtt5::NegotiatedSettings> settings)
            {
                client.isConnected = true;
                client.clientId = Aws::Crt::String(settings->getClientId().c_str(), settings->getClientId().length());
                client.settings = settings;

                AWS_LOGF_INFO(
                    AWS_LS_MQTT5_CANARY, "ID:%s Lifecycle Event: Connection Success", client.clientId.c_str());
            });

        mqtt5Options.withClientConnectionFailureCallback(
            [&client](Mqtt5::Mqtt5Client &, int error_code, std::shared_ptr<Aws::Crt::Mqtt5::ConnAckPacket>) {
                AWS_LOGF_INFO(
                    AWS_LS_MQTT5_CANARY,
                    "ID:%s Connection failed with  Error Code: %d(%s)",
                    client.clientId.c_str(),
                    error_code,
                    aws_error_debug_str(error_code));
            });

        mqtt5Options.withClientDisconnectionCallback(
            [&client](
                Aws::Crt::Mqtt5::Mqtt5Client &,
                int /*errorCode*/,
                std::shared_ptr<Aws::Crt::Mqtt5::DisconnectPacket> packet_disconnect)
            {
                client.isConnected = false;
                AWS_LOGF_INFO(AWS_LS_MQTT5_CANARY, "ID:%s Lifecycle Event: Disconnect", client.clientId.c_str());
            });

        mqtt5Options.withClientStoppedCallback(
            [&client](Aws::Crt::Mqtt5::Mqtt5Client &)
            { AWS_LOGF_INFO(AWS_LS_MQTT5_CANARY, "ID:%s Lifecycle Event: Stopped", client.clientId.c_str()); });

        client.client = Mqtt5::Mqtt5Client::NewMqtt5Client(mqtt5Options, appCtx.allocator);

        awsMqtt5CanaryOperationFn *operation_fn =
            s_AwsMqtt5CanaryOperationTable.operationByOperationType[AWS_MQTT5_CANARY_OPERATION_START];
        (*operation_fn)(&client, appCtx.allocator);

        aws_thread_current_sleep(AWS_MQTT5_CANARY_CLIENT_CREATION_SLEEP_TIME);
        clients.push_back(client);
    }

    fprintf(stderr, "Clients created\n");

    /**********************************************************
     * TESTING
     **********************************************************/
    bool done = false;
    size_t operationsExecuted = 0;
    uint64_t timeTestFinish = 0;
    aws_high_res_clock_get_ticks(&timeTestFinish);
    timeTestFinish +=
        aws_timestamp_convert(testerOptions.testRunSeconds, AWS_TIMESTAMP_SECS, AWS_TIMESTAMP_NANOS, NULL);
    uint64_t timeInterval =
        aws_timestamp_convert(testerOptions.memoryCheckIntervalSec, AWS_TIMESTAMP_SECS, AWS_TIMESTAMP_NANOS, NULL);
    uint64_t memoryCheckPoint = 0;

    printf("Running test for %zu seconds\n", testerOptions.testRunSeconds);

    while (!done)
    {
        uint64_t now = 0;
        aws_high_res_clock_get_ticks(&now);
        operationsExecuted++;

        AwsMqtt5CanaryOperations nextOperation = s_AwsMqtt5CanaryGetRandomOperation(&testerOptions);
        awsMqtt5CanaryOperationFn *operation_fn =
            s_AwsMqtt5CanaryOperationTable.operationByOperationType[nextOperation];

        (*operation_fn)(&clients[rand() % testerOptions.clientCount], appCtx.allocator);

        if (now > timeTestFinish)
        {
            done = true;
        }

        if (now > memoryCheckPoint)
        {
            const size_t outstanding_bytes = aws_mem_tracer_bytes(allocator);
            printf("Summary:\n");
            printf("   Outstanding bytes: %zu\n", outstanding_bytes);
            printf("   Operations executed: %zu\n", operationsExecuted);
            printf("   Operating TPS average over test: %zu\n\n", operationsExecuted / testerOptions.testRunSeconds);
            memoryCheckPoint = now + timeInterval;
        }

        aws_thread_current_sleep(testerOptions.tpsSleepTime);
    }
    /**********************************************************
     * CLEAN UP
     **********************************************************/
    clients.clear();

    return 0;
}
