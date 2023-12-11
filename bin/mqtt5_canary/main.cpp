/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Api.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/UUID.h>
#include <aws/crt/crypto/Hash.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/TlsOptions.h>
#include <aws/crt/io/Uri.h>

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
using namespace Aws::Crt::Mqtt5;

struct AppCtx
{
    Allocator *allocator;
    struct aws_mutex lock;
    Io::Uri uri;
    uint32_t port;
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
                break;
            case 't':
                testerOptions->elgMaxThreads = (uint16_t)atoi(aws_cli_optarg);
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
                    fprintf(stderr, "unsupported log level %s\n", aws_cli_optarg);
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
                    fprintf(
                        stderr,
                        "Failed to parse uri %s with error %s\n",
                        aws_cli_positional_arg,
                        aws_error_debug_str(ctx.uri.LastError()));
                    s_Usage(1);
                }
                else
                {
                    fprintf(stderr, "Succeed to parse uri " PRInSTR "\n", AWS_BYTE_CURSOR_PRI(ctx.uri.GetFullUri()));
                }
                break;
            default:
                fprintf(stderr, "Unknown option\n");
                s_Usage(1);
        }
    }

    if (!ctx.uri)
    {
        fprintf(stderr, "A URI for the request must be supplied.\n");
        s_Usage(1);
    }
}

/**********************************************************
 * MQTT5 CANARY OPTIONS
 **********************************************************/

static void s_Mqtt5CanaryUpdateTpsSleepTime(struct AwsMqtt5CanaryTesterOptions *testerOptions)
{

    testerOptions->tpsSleepTime =
        testerOptions->tps == 0
            ? 0
            : (aws_timestamp_convert(1, AWS_TIMESTAMP_SECS, AWS_TIMESTAMP_NANOS, NULL) / testerOptions->tps);
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
    /* Time interval for printing memory usage info in seconds. Default to 10 mins */
    testerOptions->memoryCheckIntervalSec = 600;
}

struct AwsMqtt5CanaryStatistic
{
    uint64_t totalOperations;

    uint64_t subscribe_attempt;
    uint64_t subscribe_succeed;
    uint64_t subscribe_failed;

    uint64_t publish_attempt;
    uint64_t publish_succeed;
    uint64_t publish_failed;

    uint64_t unsub_attempt;
    uint64_t unsub_succeed;
    uint64_t unsub_failed;
} g_statistic;

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
        AWS_LOGF_ERROR(AWS_LS_MQTT5_CANARY, "Invalid Client, Client Creation Failed.");
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
        // Set isConnected flag to "true" to prevent calling "Start" again on the same client.
        // If the connection operation failed eventually, "withClientConnectionFailureCallback"
        // will set the flag to false.
        testClient->isConnected = true;
        return AWS_OP_SUCCESS;
    }
    AWS_LOGF_ERROR(AWS_LS_MQTT5_CANARY, "ID:%s Start Failed", testClient->clientId.c_str());
    return AWS_OP_ERR;
}

static int s_AwsMqtt5CanaryOperationStop(struct AwsMqtt5CanaryTestClient *testClient, Allocator * /*allocator*/)
{
    if (!testClient->isConnected)
    {
        return AWS_OP_SUCCESS;
    }

    g_statistic.totalOperations++;
    if (testClient->client->Stop())
    {
        testClient->subscriptionCount = 0;
        AWS_LOGF_INFO(AWS_LS_MQTT5_CANARY, "ID:%s Stop", testClient->clientId.c_str());
        return AWS_OP_SUCCESS;
    }
    AWS_LOGF_ERROR(AWS_LS_MQTT5_CANARY, "ID:%s Stop Failed", testClient->clientId.c_str());
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
    subscription1.WithTopicFilter(Aws::Crt::String(topicArray))
        .WithNoLocal(false)
        .WithQOS(Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE)
        .WithRetainHandlingType(Mqtt5::RetainHandlingType::AWS_MQTT5_RHT_SEND_ON_SUBSCRIBE)
        .WithRetainAsPublished(false);

    Mqtt5::Subscription subscription2;
    subscription2.WithTopicFilter(testClient->sharedTopic)
        .WithNoLocal(false)
        .WithQOS(Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE)
        .WithRetainHandlingType(Mqtt5::RetainHandlingType::AWS_MQTT5_RHT_SEND_ON_SUBSCRIBE)
        .WithRetainAsPublished(false);

    std::shared_ptr<Mqtt5::SubscribePacket> packet = std::make_shared<Mqtt5::SubscribePacket>(allocator);
    packet->WithSubscription(std::move(subscription1));
    packet->WithSubscription(std::move(subscription2));

    testClient->subscriptionCount++;

    ++g_statistic.totalOperations;
    ++g_statistic.subscribe_attempt;
    AWS_LOGF_INFO(AWS_LS_MQTT5_CANARY, "ID:%s Subscribe to topic: %s", testClient->clientId.c_str(), topicArray);

    if (testClient->client->Subscribe(packet, [](int errorcode, std::shared_ptr<SubAckPacket>) {
            if (errorcode != 0)
            {
                ++g_statistic.subscribe_failed;
                AWS_LOGF_ERROR(
                    AWS_LS_MQTT5_CANARY,
                    "Subscribe failed with errorcode: %d, %s\n",
                    errorcode,
                    aws_error_str(errorcode));
                return;
            }
            ++g_statistic.subscribe_succeed;
        }))
    {
        return AWS_OP_SUCCESS;
    }
    ++g_statistic.subscribe_failed;
    AWS_LOGF_ERROR(AWS_LS_MQTT5_CANARY, "ID:%s Subscribe Failed", testClient->clientId.c_str());
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
    unsubscription->WithTopicFilters(topics);

    ++g_statistic.totalOperations;
    ++g_statistic.unsub_attempt;
    if (testClient->client->Unsubscribe(
            unsubscription, [testClient](int, std::shared_ptr<Mqtt5::UnSubAckPacket> packet) {
                if (packet == nullptr)
                    return;
                if (packet->getReasonCodes()[0] == AWS_MQTT5_UARC_SUCCESS)
                {
                    ++g_statistic.unsub_succeed;
                    AWS_LOGF_ERROR(
                        AWS_LS_MQTT5_CANARY,
                        "ID:%s Unsubscribe Bad Server Failed with errorcode : %s\n",
                        testClient->clientId.c_str(),
                        packet->getReasonString()->c_str());
                }
            }))
    {
        AWS_LOGF_INFO(AWS_LS_MQTT5_CANARY, "ID:%s Unsubscribe Bad", testClient->clientId.c_str());
        return AWS_OP_SUCCESS;
    }
    ++g_statistic.unsub_failed;
    AWS_LOGF_ERROR(AWS_LS_MQTT5_CANARY, "ID:%s Unsubscribe Bad Operation Failed", testClient->clientId.c_str());
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
    unsubscription->WithTopicFilters(topics);

    ++g_statistic.totalOperations;
    ++g_statistic.unsub_attempt;
    if (testClient->client->Unsubscribe(unsubscription))
    {
        ++g_statistic.unsub_succeed;
        AWS_LOGF_INFO(
            AWS_LS_MQTT5_CANARY, "ID:%s Unsubscribe from topic: %s", testClient->clientId.c_str(), topicArray);
        return AWS_OP_SUCCESS;
    }
    ++g_statistic.unsub_failed;
    AWS_LOGF_ERROR(AWS_LS_MQTT5_CANARY, "ID:%s Unsubscribe Failed", testClient->clientId.c_str());
    return AWS_OP_ERR;
}

/* Help function for Publish Operation. Do not call it directly for operations. */
static int s_AwsMqtt5CanaryOperationPublish(
    struct AwsMqtt5CanaryTestClient *testClient,
    Aws::Crt::String topicFilter,
    Mqtt5::QOS qos,
    Allocator *allocator)
{
    /* Create a property value with random size */
    uint16_t up_size = (rand() % UINT16_MAX) / 2 + 1;
    char up_data[AWS_MQTT5_CANARY_PAYLOAD_SIZE_MAX];
    AWS_ZERO_STRUCT(up_data);
    size_t i = 0;
    for (i = 0; i < up_size; i++)
    {
        up_data[i] = 'A';
    }
    up_data[i] = 0;

    Mqtt5::UserProperty up1("property1", up_data);
    Mqtt5::UserProperty up2("property2", up_data);
    Mqtt5::UserProperty up3("property3", up_data);

    uint16_t payload_size = 1;
    uint8_t payload_data[AWS_MQTT5_CANARY_PAYLOAD_SIZE_MAX];
    for (i = 0; i < payload_size; i++)
    {
        payload_data[i] = rand() % 128 + 1;
    }
    ByteCursor payload = ByteCursorFromArray(payload_data, payload_size);

    std::shared_ptr<Mqtt5::PublishPacket> packetPublish = std::make_shared<Mqtt5::PublishPacket>(allocator);
    packetPublish->WithTopic(topicFilter)
        .WithQOS(qos)
        .WithRetain(false)
        .WithPayload(payload)
        .WithUserProperty(std::move(up1))
        .WithUserProperty(std::move(up2))
        .WithUserProperty(std::move(up3));

    ++g_statistic.totalOperations;
    ++g_statistic.publish_attempt;

    if (testClient->client->Publish(packetPublish, [testClient](int errorcode, std::shared_ptr<PublishResult> packet) {
            if (errorcode != 0)
            {
                ++g_statistic.publish_failed;
                AWS_LOGF_ERROR(
                    AWS_LS_MQTT5_CANARY,
                    "ID: %s Publish failed with error code: %d, %s\n",
                    testClient->clientId.c_str(),
                    errorcode,
                    aws_error_str(errorcode));
                return;
            }
            ++g_statistic.publish_succeed;
        }))
    {
        AWS_LOGF_INFO(
            AWS_LS_MQTT5_CANARY, "ID:%s Publish to topic %s", testClient->clientId.c_str(), topicFilter.c_str());
        return AWS_OP_SUCCESS;
    }
    ++g_statistic.publish_failed;
    AWS_LOGF_INFO(AWS_LS_MQTT5_CANARY, "ID:%s Publish Failed\n", testClient->clientId.c_str());
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

static struct AwsMqtt5CanaryOperationsFunctionTable s_AwsMqtt5CanaryOperationTable = {{
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
}};

/**********************************************************
 * MAIN
 **********************************************************/

int main(int argc, char **argv)
{
    struct aws_allocator *allocator = aws_mem_tracer_new(aws_default_allocator(), NULL, AWS_MEMTRACE_STACKS, 15);

    {
        ApiHandle apiHandle(allocator);
        struct AppCtx appCtx = {};
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

        AWS_ZERO_STRUCT(g_statistic);

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

        if (appCtx.TraceFile)
        {
            apiHandle.InitializeLogging(appCtx.LogLevel, appCtx.TraceFile);
        }
        else
        {
            apiHandle.InitializeLogging(appCtx.LogLevel, stderr);
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
                    AWS_LOGF_ERROR(
                        AWS_LS_MQTT5_CANARY,
                        "Failed to load %s and %s with error %s.",
                        appCtx.cert,
                        appCtx.key,
                        aws_error_debug_str(tlsCtxOptions.LastError()));
                    exit(1);
                }
            }
            else
            {
                tlsCtxOptions = Io::TlsContextOptions::InitDefaultClient();
                if (!tlsCtxOptions)
                {
                    AWS_LOGF_ERROR(
                        AWS_LS_MQTT5_CANARY,
                        "Failed to create a default tlsCtxOptions with error %s",
                        aws_error_debug_str(tlsCtxOptions.LastError()));
                    exit(1);
                }
            }

            tlsContext = Io::TlsContext(tlsCtxOptions, Io::TlsMode::CLIENT, appCtx.allocator);

            tlsConnectionOptions = tlsContext.NewConnectionOptions();

            if (!tlsConnectionOptions.SetServerName(hostName))
            {
                AWS_LOGF_ERROR(
                    AWS_LS_MQTT5_CANARY,
                    "Failed to set servername with error %s",
                    aws_error_debug_str(tlsConnectionOptions.LastError()));
                exit(1);
            }
            if (!tlsConnectionOptions.SetAlpnList("x-amzn-mqtt-ca"))
            {
                AWS_LOGF_ERROR(
                    AWS_LS_MQTT5_CANARY,
                    "Failed to set alpn list with error %s",
                    aws_error_debug_str(tlsConnectionOptions.LastError()));
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
            AWS_LOGF_ERROR(
                AWS_LS_MQTT5_CANARY,
                "Failed to create eventloop group with error %s",
                aws_error_debug_str(eventLoopGroup.LastError()));
            exit(1);
        }

        Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, appCtx.allocator);
        if (!defaultHostResolver)
        {
            AWS_LOGF_ERROR(
                AWS_LS_MQTT5_CANARY,
                "Failed to create host resolver with error %s",
                aws_error_debug_str(defaultHostResolver.LastError()));
            exit(1);
        }

        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);

        if (!clientBootstrap)
        {
            AWS_LOGF_ERROR(
                AWS_LS_MQTT5_CANARY,
                "Failed to create client bootstrap with error %s",
                aws_error_debug_str(clientBootstrap.LastError()));
            exit(1);
        }

        /**********************************************************
         * MQTT5 CLIENT CREATION
         **********************************************************/

        uint16_t receive_maximum = 9;
        uint32_t maximum_packet_size = 128 * 1024;

        std::shared_ptr<Mqtt5::ConnectPacket> packetConnect = std::make_shared<Mqtt5::ConnectPacket>(allocator);
        packetConnect->WithKeepAliveIntervalSec(30)
            .WithMaximumPacketSizeBytes(maximum_packet_size)
            .WithReceiveMaximum(receive_maximum);

        Aws::Crt::String namestring((const char *)hostName.ptr, hostName.len);
        Aws::Crt::Mqtt5::Mqtt5ClientOptions mqtt5Options(appCtx.allocator);
        mqtt5Options.WithHostName(namestring)
            .WithPort(appCtx.port)
            .WithConnectOptions(packetConnect)
            .WithSocketOptions(socketOptions)
            .WithBootstrap(&clientBootstrap)
            .WithPingTimeoutMs(10000)
            .WithReconnectOptions({AWS_EXPONENTIAL_BACKOFF_JITTER_NONE, 1000, 120000, 3000})
            .WithConnackTimeoutMs(3000);

        if (appCtx.use_tls)
        {
            mqtt5Options.WithTlsConnectionOptions(tlsConnectionOptions);
        }

        if (appCtx.use_websockets)
        {
            mqtt5Options.WithWebsocketHandshakeTransformCallback(s_AwsMqtt5TransformWebsocketHandshakeFn);
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
            client = {};
            Aws::Crt::UUID uuid;
            client.clientId = String("TestClient") + std::to_string(i).c_str() + "_" + uuid.ToString();
            client.sharedTopic = Aws::Crt::String(sharedTopicArray);
            client.isConnected = false;
            clients.push_back(client);
            mqtt5Options.WithAckTimeoutSeconds(10);
            mqtt5Options.WithPublishReceivedCallback([&clients, i](const Mqtt5::PublishReceivedEventData &publishData) {
                AWS_LOGF_INFO(
                    AWS_LS_MQTT5_CANARY,
                    "Client:%s Publish Received on topic %s",
                    clients[i].clientId.c_str(),
                    publishData.publishPacket->getTopic().c_str());
            });

            mqtt5Options.WithClientConnectionSuccessCallback(
                [&clients, i](const Mqtt5::OnConnectionSuccessEventData &eventData) {
                    clients[i].isConnected = true;
                    clients[i].clientId = Aws::Crt::String(
                        eventData.negotiatedSettings->getClientId().c_str(),
                        eventData.negotiatedSettings->getClientId().length());
                    clients[i].settings = eventData.negotiatedSettings;

                    AWS_LOGF_INFO(
                        AWS_LS_MQTT5_CANARY, "ID:%s Lifecycle Event: Connection Success", clients[i].clientId.c_str());
                });

            mqtt5Options.WithClientConnectionFailureCallback(
                [&clients, i](const OnConnectionFailureEventData &eventData) {
                    clients[i].isConnected = false;
                    AWS_LOGF_ERROR(
                        AWS_LS_MQTT5_CANARY,
                        "ID:%s Connection failed with  Error Code: %d(%s)",
                        clients[i].clientId.c_str(),
                        eventData.errorCode,
                        aws_error_debug_str(eventData.errorCode));
                });

            mqtt5Options.WithClientDisconnectionCallback([&clients, i](const OnDisconnectionEventData &) {
                clients[i].isConnected = false;
                AWS_LOGF_ERROR(AWS_LS_MQTT5_CANARY, "ID:%s Lifecycle Event: Disconnect", clients[i].clientId.c_str());
            });

            mqtt5Options.WithClientStoppedCallback([&clients, i](const OnStoppedEventData &) {
                clients[i].isConnected = false;
                AWS_LOGF_ERROR(AWS_LS_MQTT5_CANARY, "ID:%s Lifecycle Event: Stopped", clients[i].clientId.c_str());
            });

            clients[i].client = Mqtt5::Mqtt5Client::NewMqtt5Client(mqtt5Options, appCtx.allocator);
            if (clients[i].client == nullptr)
            {
                AWS_LOGF_ERROR(AWS_LS_MQTT5_CANARY, "ID:%s Client Creation Failed.", client.clientId.c_str());
                continue;
            }

            awsMqtt5CanaryOperationFn *operation_fn =
                s_AwsMqtt5CanaryOperationTable.operationByOperationType[AWS_MQTT5_CANARY_OPERATION_START];
            if ((*operation_fn)(&clients[i], appCtx.allocator) == AWS_OP_ERR)
            {
                AWS_LOGF_ERROR(AWS_LS_MQTT5_CANARY, "ID:%s Operation Failed.", client.clientId.c_str());
            }

            aws_thread_current_sleep(AWS_MQTT5_CANARY_CLIENT_CREATION_SLEEP_TIME);
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

            (*operation_fn)(&clients[rand() % clients.size()], appCtx.allocator);

            if (now > timeTestFinish)
            {
                fprintf(
                    stderr,
                    "   Operating TPS average over test: %zu\n\n",
                    operationsExecuted / testerOptions.testRunSeconds);
                done = true;
            }

            if (now > memoryCheckPoint)
            {
                const size_t outstanding_bytes = aws_mem_tracer_bytes(allocator);
                fprintf(stderr, "Summary:\n");
                fprintf(stderr, "   Outstanding bytes: %zu\n", outstanding_bytes);
                fprintf(stderr, "   Operations executed: %zu\n", operationsExecuted);
                memoryCheckPoint = now + timeInterval;
            }

            aws_thread_current_sleep(testerOptions.tpsSleepTime);
        }
        /**********************************************************
         * CLEAN UP
         **********************************************************/

        for (auto client : clients)
        {
            awsMqtt5CanaryOperationFn *operation_fn =
                s_AwsMqtt5CanaryOperationTable.operationByOperationType[AWS_MQTT5_CANARY_OPERATION_STOP];
            if ((*operation_fn)(&client, appCtx.allocator) == AWS_OP_ERR)
            {
                AWS_LOGF_ERROR(AWS_LS_MQTT5_CANARY, "ID:%s STOP Operation Failed.", client.clientId.c_str());
            }
        }

        fprintf(
            stderr,
            "Final Statistic: \n"
            "total operations: %" PRId64 "\n"
            "tps: %" PRId64 "\n"
            "subscribe attempt: %" PRId64 "\n"
            "subscribe succeed: %" PRId64 "\n"
            "subscribe failed: %" PRId64 "\n"
            "publish attempt: %" PRId64 "\n"
            "publish succeed: %" PRId64 "\n"
            "publish failed: %" PRId64 "\n"
            "unsub attempt: %" PRId64 "\n"
            "unsub succeed: %" PRId64 "\n"
            "unsub failed: %" PRId64 "\n",
            g_statistic.totalOperations,
            g_statistic.totalOperations / testerOptions.testRunSeconds,
            g_statistic.subscribe_attempt,
            g_statistic.subscribe_succeed,
            g_statistic.subscribe_failed,
            g_statistic.publish_attempt,
            g_statistic.publish_succeed,
            g_statistic.publish_failed,
            g_statistic.unsub_attempt,
            g_statistic.unsub_succeed,
            g_statistic.unsub_failed);
    }

    aws_mem_tracer_destroy(allocator);

    return 0;
}
