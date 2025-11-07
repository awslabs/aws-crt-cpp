/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Api.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/crypto/Hash.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/Socks5ProxyOptions.h>
#include <aws/crt/io/Uri.h>

#include <aws/crt/mqtt/Mqtt5Packets.h>
#include <aws/iot/MqttCommon.h>

#include <algorithm>
#include <aws/common/allocator.h>
#include <aws/common/command_line_parser.h>
#include <aws/common/error.h>
#include <aws/common/string.h>
#include <cctype>
#include <condition_variable>
#include <fstream>
#include <future>
#include <iostream>

using namespace Aws::Crt;
using namespace Aws::Crt::Mqtt5;

enum class CredentialsProviderSource {
    DefaultChain,
    Environment,
    Profile,
    Static
};

struct app_ctx
{
    Allocator *allocator = nullptr;
    Io::Uri uri;
    uint32_t port = 0;
    const char *cacert = nullptr;
    const char *cert = nullptr;
    const char *key = nullptr;
    int connect_timeout = 0;

    aws_tls_connection_options tls_connection_options;

    const char *TraceFile = nullptr;
    Aws::Crt::LogLevel LogLevel = Aws::Crt::LogLevel::None;

    Aws::Crt::String proxy_host_storage;
    uint16_t proxy_port = 0;
    bool use_proxy = false;
    Aws::Crt::Optional<Io::Socks5ProxyOptions> socks5_proxy_options;

    bool enable_tls = false;
    bool use_websocket = false;
    Aws::Crt::String region;
    CredentialsProviderSource credentials_source = CredentialsProviderSource::DefaultChain;
    Aws::Crt::String profile_name;
    Aws::Crt::String config_file;
    Aws::Crt::String credentials_file;
    Aws::Crt::String access_key_id;
    Aws::Crt::String secret_access_key;
    Aws::Crt::String session_token;
    bool port_overridden = false;
    bool use_ipv6 = false;
};

static bool s_parse_proxy_uri(app_ctx &ctx, const char *proxy_arg)
{
    if (!proxy_arg || proxy_arg[0] == '\0')
    {
        std::cerr << "Proxy URI must not be empty\n";
        return false;
    }

    ByteCursor uri_cursor = aws_byte_cursor_from_c_str(proxy_arg);
    Io::Uri parsed_uri(uri_cursor, ctx.allocator);
    if (!parsed_uri)
    {
        std::cerr << "Failed to parse proxy URI \"" << proxy_arg
                  << "\": " << aws_error_debug_str(parsed_uri.LastError()) << std::endl;
        return false;
    }

    auto proxyOptions = Io::Socks5ProxyOptions::CreateFromUri(parsed_uri, 10000, ctx.allocator);
    if (!proxyOptions)
    {
        std::cerr << "Failed to create SOCKS5 proxy options from \"" << proxy_arg
                  << "\": " << aws_error_debug_str(Aws::Crt::LastError()) << std::endl;
        return false;
    }

    ctx.socks5_proxy_options = *proxyOptions;

    ByteCursor host_cursor = parsed_uri.GetHostName();
    ctx.proxy_host_storage.assign(reinterpret_cast<const char *>(host_cursor.ptr), host_cursor.len);

    uint32_t port = parsed_uri.GetPort();
    if (port == 0)
    {
        port = 1080;
    }
    ctx.proxy_port = static_cast<uint16_t>(port);

    ctx.use_proxy = true;
    return true;
}

static void s_usage(int exit_code)
{
    fprintf(stderr, "usage: mqtt_socks5_cpp_example [options]\n");
    fprintf(stderr, " --broker-host HOST: MQTT broker hostname (default: test.mosquitto.org)\n");
    fprintf(stderr, " --broker-port PORT: MQTT broker port (default: 1883 for MQTT, 8883 for MQTTS)\n");
    fprintf(stderr, " --proxy URL: SOCKS5 proxy URI (socks5h://... for proxy DNS, socks5://... for local DNS)\n");
    fprintf(stderr, " --cert FILE: Client certificate file path (PEM format)\n");
    fprintf(stderr, " --key FILE: Private key file path (PEM format)\n");
    fprintf(stderr, " --ca-file FILE: CA certificate file path (PEM format)\n");
    fprintf(stderr, " --websocket: Use MQTT over WebSocket with SigV4 authentication\n");
    fprintf(stderr, " --region REGION: AWS Region for SigV4 signing when using WebSocket\n");
    fprintf(stderr,
            " --credential-source SOURCE: Credentials provider source (default-chain, environment, profile, static)\n");
    fprintf(stderr, " --profile NAME: AWS profile to use when credential source is profile\n");
    fprintf(stderr, " --config-file PATH: AWS config file override for profile credential source\n");
    fprintf(stderr, " --credentials-file PATH: AWS credentials file override for profile credential source\n");
    fprintf(stderr, " --access-key KEY: AWS access key for static credential source\n");
    fprintf(stderr, " --secret-key KEY: AWS secret access key for static credential source\n");
    fprintf(stderr, " --session-token TOKEN: AWS session token for static credential source (optional)\n");
    fprintf(stderr, " --ipv6: Force IPv6 socket domain\n");
    fprintf(stderr, " --verbose: Print detailed logging\n");
    fprintf(stderr, " --help: Display this message and exit\n");
    exit(exit_code);
}

static struct aws_cli_option s_long_options[] = {
    {"broker-host", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'b'},
    {"broker-port", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'p'},
    {"proxy", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'x'},
    {"cert", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'C'},
    {"key", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'K'},
    {"ca-file", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'A'},
    {"websocket", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'W'},
    {"ipv6", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, '6'},
    {"region", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'R'},
    {"credential-source", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'S'},
    {"profile", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'P'},
    {"config-file", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'F'},
    {"credentials-file", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'G'},
    {"access-key", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'I'},
    {"secret-key", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'J'},
    {"session-token", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'T'},
    {"verbose", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'v'},
    {"help", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'h'},
    {NULL, AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 0}, // Ensure proper termination
};

static void s_parse_options(int argc, char **argv, struct app_ctx &ctx)
{
    ctx.use_proxy = false;
    ctx.proxy_host_storage.clear();
    ctx.proxy_port = 0;
    ctx.socks5_proxy_options.reset();

    while (true)
    {
        int option_index = 0;
        int c = aws_cli_getopt_long(argc, argv, "b:p:x:C:K:A:W6R:S:P:F:G:I:J:T:vh", s_long_options, &option_index);
        if (c == -1)
        {
            break;
        }

        switch (c)
        {
            case 'b':
                ctx.uri = Io::Uri(aws_byte_cursor_from_c_str(aws_cli_optarg), ctx.allocator);
                break;
            case 'p':
                ctx.port = static_cast<uint16_t>(atoi(aws_cli_optarg));
                ctx.port_overridden = true;
                break;
            case 'x':
                if (!s_parse_proxy_uri(ctx, aws_cli_optarg))
                {
                    s_usage(1);
                }
                break;
            case 'C':
                ctx.cert = aws_cli_optarg;
                break;
            case 'K':
                ctx.key = aws_cli_optarg;
                break;
            case 'A':
                ctx.cacert = aws_cli_optarg;
                break;
            case 'W':
                ctx.use_websocket = true;
                break;
            case '6':
                ctx.use_ipv6 = true;
                break;
            case 'R':
                ctx.region = aws_cli_optarg;
                break;
            case 'S': {
                Aws::Crt::String source = aws_cli_optarg;
                std::transform(source.begin(), source.end(), source.begin(), [](unsigned char ch) {
                    return static_cast<char>(std::tolower(ch));
                });
                if (source == "default-chain") {
                    ctx.credentials_source = CredentialsProviderSource::DefaultChain;
                } else if (source == "environment") {
                    ctx.credentials_source = CredentialsProviderSource::Environment;
                } else if (source == "profile") {
                    ctx.credentials_source = CredentialsProviderSource::Profile;
                } else if (source == "static") {
                    ctx.credentials_source = CredentialsProviderSource::Static;
                } else {
                    std::cerr << "Unknown credential source '" << aws_cli_optarg
                              << "'. Expected one of: default-chain, environment, profile, static." << std::endl;
                    s_usage(1);
                }
                break;
            }
            case 'P':
                ctx.profile_name = aws_cli_optarg;
                break;
            case 'F':
                ctx.config_file = aws_cli_optarg;
                break;
            case 'G':
                ctx.credentials_file = aws_cli_optarg;
                break;
            case 'I':
                ctx.access_key_id = aws_cli_optarg;
                break;
            case 'J':
                ctx.secret_access_key = aws_cli_optarg;
                break;
            case 'T':
                ctx.session_token = aws_cli_optarg;
                break;
            case 'v':
                ctx.LogLevel = Aws::Crt::LogLevel::Trace;
                break;
            case 'h':
                s_usage(0);
                break;
            default:
                std::cerr << "Unknown option\n";
                s_usage(1);
        }
    }

    if (ctx.use_websocket)
    {
        ctx.enable_tls = true;
        if (!ctx.port_overridden && ctx.port == 1883 && !ctx.uri.GetPort())
        {
            ctx.port = 443;
        }
    }
    if (!ctx.enable_tls) {
        ctx.enable_tls = ctx.cacert || ctx.cert || ctx.key;
    }
}

/**********************************************************
 * MAIN
 **********************************************************/

/**
 * This example demonstrates basic MQTT5 client functionality using SOCKS5 proxy and optional TLS/WebSocket.
 *
 * It is primarily used for integration tests to validate end-to-end connectivity and message flow
 * with different combinations of proxy, TLS, and WebSocket options.
 *
 * The workflow for the application is:
 *  1. Connect to the MQTT broker (optionally via SOCKS5 proxy, TLS, and/or WebSocket).
 *  2. Subscribe to the topic "test/topic/test1" with QoS 1.
 *  3. Publish the message "mqtt5 publish test" to "test/topic/test1".
 *  4. Wait to receive the published message back on the subscribed topic.
 *  5. Disconnect from the broker and exit.
 *
 * This example does not require user interaction and does not demonstrate multiple subscriptions or unsubscriptions.
 * It is intended as a minimal end-to-end test of connect, subscribe, publish, receive, and disconnect using various
 * connection options.
 */

void PrintAppOptions(const app_ctx &ctx)
{
    std::cout << "================= MQTT5 SOCKS5 APP OPTIONS =================" << std::endl;
    Aws::Crt::String hostNameStr =
        Aws::Crt::String((const char *)ctx.uri.GetHostName().ptr, ctx.uri.GetHostName().len);
    std::cout << "Broker Host: " << hostNameStr << std::endl;
    std::cout << "Broker Port: " << ctx.port << std::endl;
    std::cout << "TLS Enabled: " << (ctx.enable_tls ? "yes" : "no") << std::endl;
    if (ctx.cacert) std::cout << "CA Cert: " << ctx.cacert << std::endl;
    if (ctx.cert && !ctx.use_websocket) std::cout << "Client Cert: " << ctx.cert << std::endl;
    if (ctx.key && !ctx.use_websocket) std::cout << "Client Key: " << ctx.key << std::endl;
    std::cout << "Connect Timeout (ms): " << ctx.connect_timeout << std::endl;
    if (ctx.use_websocket) {
        std::cout << "Using WebSocket: yes" << std::endl;
        if (!ctx.region.empty()) {
            std::cout << "AWS Region: " << ctx.region << std::endl;
        }
        std::cout << "Credentials Source: ";
        switch (ctx.credentials_source) {
            case CredentialsProviderSource::DefaultChain:
                std::cout << "default-chain";
                break;
            case CredentialsProviderSource::Environment:
                std::cout << "environment";
                break;
            case CredentialsProviderSource::Profile:
                std::cout << "profile";
                if (!ctx.profile_name.empty()) {
                    std::cout << " (profile=" << ctx.profile_name << ")";
                }
                if (!ctx.config_file.empty()) {
                    std::cout << " (config-file=" << ctx.config_file << ")";
                }
                if (!ctx.credentials_file.empty()) {
                    std::cout << " (credentials-file=" << ctx.credentials_file << ")";
                }
                break;
            case CredentialsProviderSource::Static:
                std::cout << "static";
                if (!ctx.access_key_id.empty()) {
                    std::cout << " (access-key provided)";
                }
                if (!ctx.session_token.empty()) {
                    std::cout << " (session token provided)";
                }
                break;
        }
        std::cout << std::endl;
    } else {
        std::cout << "Using WebSocket: no" << std::endl;
    }
    std::cout << "Socket Domain: " << (ctx.use_ipv6 ? "IPv6" : "IPv4") << std::endl;
    if (ctx.use_proxy && ctx.socks5_proxy_options && !ctx.proxy_host_storage.empty()) {
        std::cout << "SOCKS5 Proxy Host: " << ctx.proxy_host_storage << std::endl;
        std::cout << "SOCKS5 Proxy Port: " << ctx.proxy_port << std::endl;
        bool resolveViaProxy =
            ctx.socks5_proxy_options->GetHostResolutionMode() == Io::AwsSocks5HostResolutionMode::Proxy;
        std::cout << "SOCKS5 DNS Resolution: " << (resolveViaProxy ? "proxy" : "client") << std::endl;
        const aws_socks5_proxy_options *rawProxyOptions = ctx.socks5_proxy_options->GetUnderlyingHandle();
        if (rawProxyOptions->username && rawProxyOptions->password)
        {
            std::cout << "SOCKS5 Proxy Auth: username='" << aws_string_c_str(rawProxyOptions->username)
                      << "', password=***" << std::endl;
        }
        else
        {
            std::cout << "SOCKS5 Proxy Auth: none" << std::endl;
        }
    }
    else
    {
        std::cout << "SOCKS5 Proxy: not configured" << std::endl;
    }
    std::cout << "============================================================" << std::endl;
}

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

    if (app_ctx.use_websocket)
    {
        if (app_ctx.region.empty())
        {
            std::cerr << "[ERROR] --region must be specified when using --websocket for SigV4 authentication."
                      << std::endl;
            return 1;
        }

        if (app_ctx.credentials_source == CredentialsProviderSource::Static &&
            (app_ctx.access_key_id.empty() || app_ctx.secret_access_key.empty()))
        {
            std::cerr << "[ERROR] Static credentials require both --access-key and --secret-key when using WebSocket."
                      << std::endl;
            return 1;
        }

        if (app_ctx.cert || app_ctx.key)
        {
            std::cout << "[INFO] Client certificate and key are ignored when using WebSocket SigV4 authentication."
                      << std::endl;
        }
    }

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

    bool useTls = app_ctx.use_websocket || app_ctx.enable_tls;

    auto hostName = app_ctx.uri.GetHostName();

    /***************************************************
     * setup connection configs
     ***************************************************/

    Io::TlsContextOptions tlsCtxOptions;
    Io::TlsContext tlsContext;
    Io::TlsConnectionOptions tlsConnectionOptions;
    if (useTls)
    {
        if (app_ctx.use_websocket)
        {
            std::cout << "MQTT5: Configuring TLS for WebSocket connection with SigV4 authentication." << std::endl;
            tlsCtxOptions = Io::TlsContextOptions::InitDefaultClient();
            if (!tlsCtxOptions)
            {
                std::cout << "Failed to create TLS options for WebSocket with error "
                          << aws_error_debug_str(tlsCtxOptions.LastError()) << std::endl;
                exit(1);
            }
        }
        else if (app_ctx.cert && app_ctx.key)
        {
            std::cout << "MQTT5: Configuring TLS with cert " << app_ctx.cert << " and key " << app_ctx.key << std::endl;
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
            std::cout << "MQTT5: Configuring TLS with default settings." << std::endl;
            tlsCtxOptions = Io::TlsContextOptions::InitDefaultClient();
            if (!tlsCtxOptions)
            {
                std::cout << "Failed to create a default tlsCtxOptions with error "
                          << aws_error_debug_str(tlsCtxOptions.LastError()) << std::endl;
                exit(1);
            }
        }

        if (app_ctx.cacert)
        {
            std::cout << "MQTT5: Configuring TLS with CA " << app_ctx.cacert << std::endl;
            tlsCtxOptions.OverrideDefaultTrustStore(nullptr, app_ctx.cacert);
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
    }

    Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(app_ctx.connect_timeout);
    socketOptions.SetKeepAliveIntervalSec(0);
    socketOptions.SetKeepAlive(false);
    socketOptions.SetKeepAliveTimeoutSec(0);
    socketOptions.SetSocketDomain(app_ctx.use_ipv6 ? Io::SocketDomain::IPv6 : Io::SocketDomain::IPv4);

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

    PrintAppOptions(app_ctx);

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
        std::cout << "**********************************************************" << std::endl;
        std::cout << "MQTT5: Configuring TLS...." << std::endl;
        std::cout << "**********************************************************" << std::endl;
        mqtt5OptionsBuilder.WithTlsConnectionOptions(tlsConnectionOptions);
    }

    // Configure WebSocket if requested
    std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> websocketCredentialsProvider;
    if (app_ctx.use_websocket) {
        std::cout << "**********************************************************" << std::endl;
        std::cout << "MQTT5: Configuring WebSocket with SigV4 authentication...." << std::endl;
        std::cout << "**********************************************************" << std::endl;

        Aws::Iot::WebsocketConfig websocketConfig(app_ctx.region, &clientBootstrap, app_ctx.allocator);

        switch (app_ctx.credentials_source) {
            case CredentialsProviderSource::DefaultChain:
                // Already handled by the default constructor using the client bootstrap.
                break;
            case CredentialsProviderSource::Environment:
                websocketCredentialsProvider =
                    Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderEnvironment(app_ctx.allocator);
                break;
            case CredentialsProviderSource::Profile: {
                Aws::Crt::Auth::CredentialsProviderProfileConfig profileConfig;
                profileConfig.Bootstrap = &clientBootstrap;
                if (!app_ctx.profile_name.empty()) {
                    profileConfig.ProfileNameOverride = aws_byte_cursor_from_c_str(app_ctx.profile_name.c_str());
                }
                if (!app_ctx.config_file.empty()) {
                    profileConfig.ConfigFileNameOverride =
                        aws_byte_cursor_from_c_str(app_ctx.config_file.c_str());
                }
                if (!app_ctx.credentials_file.empty()) {
                    profileConfig.CredentialsFileNameOverride =
                        aws_byte_cursor_from_c_str(app_ctx.credentials_file.c_str());
                }
                websocketCredentialsProvider =
                    Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderProfile(profileConfig, app_ctx.allocator);
                break;
            }
            case CredentialsProviderSource::Static: {
                Aws::Crt::Auth::CredentialsProviderStaticConfig staticConfig;
                staticConfig.AccessKeyId = aws_byte_cursor_from_c_str(app_ctx.access_key_id.c_str());
                staticConfig.SecretAccessKey = aws_byte_cursor_from_c_str(app_ctx.secret_access_key.c_str());
                if (!app_ctx.session_token.empty()) {
                    staticConfig.SessionToken = aws_byte_cursor_from_c_str(app_ctx.session_token.c_str());
                }
                websocketCredentialsProvider =
                    Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderStatic(staticConfig, app_ctx.allocator);
                break;
            }
        }

        if (app_ctx.credentials_source != CredentialsProviderSource::DefaultChain) {
            if (!websocketCredentialsProvider) {
                std::cerr << "[ERROR] Failed to create credentials provider for WebSocket connection." << std::endl;
                return 1;
            }
            websocketConfig = Aws::Iot::WebsocketConfig(app_ctx.region, websocketCredentialsProvider, app_ctx.allocator);
        }

        auto websocketConfigShared = std::make_shared<Aws::Iot::WebsocketConfig>(websocketConfig);

        mqtt5OptionsBuilder.WithWebsocketHandshakeTransformCallback(
            [websocketConfigShared](std::shared_ptr<Aws::Crt::Http::HttpRequest> req,
                                    const Aws::Crt::Mqtt5::OnWebSocketHandshakeInterceptComplete &onComplete) {
                auto signingComplete = [onComplete](
                                           const std::shared_ptr<Aws::Crt::Http::HttpRequest> &signedRequest,
                                           int errorCode) { onComplete(signedRequest, errorCode); };
                auto signerConfig = websocketConfigShared->CreateSigningConfigCb();
                websocketConfigShared->Signer->SignRequest(req, *signerConfig, signingComplete);
            });
    }

    if (app_ctx.use_proxy && app_ctx.socks5_proxy_options && !app_ctx.proxy_host_storage.empty())
    {
        std::cout << "**********************************************************" << std::endl;
        std::cout << "MQTT5: Configuring SOCKS5 Proxy with host " << app_ctx.proxy_host_storage << " and port "
                  << app_ctx.proxy_port << std::endl;
        bool resolveViaProxy =
            app_ctx.socks5_proxy_options->GetHostResolutionMode() == Io::AwsSocks5HostResolutionMode::Proxy;
        std::cout << "MQTT5: SOCKS5 DNS mode: " << (resolveViaProxy ? "proxy-resolved" : "client-resolved")
                  << std::endl;

        const aws_socks5_proxy_options *rawProxyOptions = app_ctx.socks5_proxy_options->GetUnderlyingHandle();
        if (rawProxyOptions->username && rawProxyOptions->password)
        {
            std::cout << "MQTT5: Configuring SOCKS5 Proxy with username " << aws_string_c_str(rawProxyOptions->username)
                      << " and password ***" << std::endl;
        }
        else
        {
            std::cout << "MQTT5: Configuring SOCKS5 Proxy with no authentication." << std::endl;
        }

        mqtt5OptionsBuilder.WithSocks5ProxyOptions(*app_ctx.socks5_proxy_options);
    }
    else
    {
        std::cout << "No SOCKS5 proxy configured." << std::endl;
    }

    std::promise<bool> connectionPromise;
    std::promise<void> stoppedPromise;
    std::promise<void> publishReceivedPromise;

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

    mqtt5OptionsBuilder.WithPublishReceivedCallback(
        [&publishReceivedPromise](const PublishReceivedEventData &eventData)
        {
            ByteCursor payload = eventData.publishPacket->getPayload();
            String msg = String((const char *)payload.ptr, payload.len);
            std::cout << "**********************************************************" << std::endl;
            std::cout << "MQTT5:Received Message: " << msg.c_str() << std::endl;
            std::cout << "**********************************************************" << std::endl;
            if (msg == "mqtt5 publish test")
            {
                publishReceivedPromise.set_value();
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

    if (!(mqtt5Client->Start() && connectionPromise.get_future().get() == true))
    {
        std::cout << "[ERROR]Failed to start the client " << std::endl;
        return 1; // Connection failure
    }

    // Subscribe to a single topic
    Mqtt5::Subscription sub(app_ctx.allocator);
    sub.WithTopicFilter("test/topic/test1").WithQOS(Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE);
    Vector<Mqtt5::Subscription> subscriptionList;
    subscriptionList.push_back(sub);
    std::shared_ptr<Mqtt5::SubscribePacket> subscribe = std::make_shared<Mqtt5::SubscribePacket>(app_ctx.allocator);
    subscribe->WithSubscriptions(subscriptionList);

    std::promise<bool> subscribeAckPromise;
    bool subscribeSuccess = mqtt5Client->Subscribe(
        subscribe,
        [&subscribeAckPromise](int errorCode, std::shared_ptr<Mqtt5::SubAckPacket> packet)
        {
            if (errorCode != AWS_ERROR_SUCCESS || packet == nullptr) {
                subscribeAckPromise.set_value(false);
                return;
            }
            std::cout << "**********************************************************" << std::endl;
            std::cout << "MQTT5: check suback packet : " << std::endl;
            bool allGranted = true;
            for (auto code : packet->getReasonCodes())
            {
                std::cout << "Get suback with codes: " << code << std::endl;
                if (code > 2) { // Non-granted reason codes are >= 0x80
                    allGranted = false;
                }
            }
            std::cout << "**********************************************************" << std::endl;
            subscribeAckPromise.set_value(allGranted);
        });

    if (!subscribeSuccess)
    {
        std::cout << "[ERROR]Subscription Failed." << std::endl;
        if (mqtt5Client->Stop())
        {
            stoppedPromise.get_future().get();
        }
        return 2; // Subscription failure
    }

    // Wait for SUBACK before publishing so we don't race the proxy latency.
    std::cout << "**********************************************************" << std::endl;
    std::cout << "MQTT5: Waiting for SUBACK confirmation..." << std::endl;
    std::cout << "**********************************************************" << std::endl;
    bool subAckGranted = false;
    try {
        subAckGranted = subscribeAckPromise.get_future().get();
    } catch (...) {
        subAckGranted = false;
    }
    if (!subAckGranted) {
        std::cout << "[ERROR]Subscription was not granted by broker." << std::endl;
        mqtt5Client->Stop();
        stoppedPromise.get_future().get();
        return 2;
    }

    // Publish to the same topic
    ByteCursor payload = Aws::Crt::ByteCursorFromCString("mqtt5 publish test");
    std::shared_ptr<Mqtt5::PublishPacket> publish = std::make_shared<Mqtt5::PublishPacket>(app_ctx.allocator);
    publish->WithTopic("test/topic/test1");
    publish->WithPayload(payload);
    publish->WithQOS(Mqtt5::QOS::AWS_MQTT5_QOS_AT_LEAST_ONCE);

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
        return 3; // Publish failure
    }

    std::cout << "**********************************************************" << std::endl;
    std::cout << "Mqtt5: Waiting for published message..." << std::endl;
    std::cout << "**********************************************************" << std::endl;
    try
    {
        publishReceivedPromise.get_future().get();
    }
    catch (...)
    {
        std::cout << "[ERROR]Did not receive published message." << std::endl;
        mqtt5Client->Stop();
        stoppedPromise.get_future().get();
        return 4; // Message not received
    }

    // Disconnect
    if (mqtt5Client->Stop())
    {
        stoppedPromise.get_future().get();
    }
    else
    {
        std::cout << "[ERROR]Failed to stop the client " << std::endl;
        return 5; // Disconnect failure
    }
    return 0;
}
