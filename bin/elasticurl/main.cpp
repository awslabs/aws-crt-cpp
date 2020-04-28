/*
 * Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Licensed under the Apache License, Version 2.0 (the "License").
 * You may not use this file except in compliance with the License.
 * A copy of the License is located at
 *
 *  http://aws.amazon.com/apache2.0
 *
 * or in the "license" file accompanying this file. This file is distributed
 * on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 * express or implied. See the License for the specific language governing
 * permissions and limitations under the License.
 */
#include <aws/crt/Api.h>
#include <aws/crt/crypto/Hash.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/Uri.h>

#include <aws/common/command_line_parser.h>

#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>

using namespace Aws::Crt;

#define ELASTICURL_VERSION "0.0.1"

std::shared_ptr<Aws::Crt::Io::IStream> input_body = nullptr;
std::ifstream inputfile;

struct elasticurl_ctx
{
    struct aws_allocator *allocator;
    const char *verb;
    Aws::Crt::Io::Uri uri;
    bool response_code_written;
    const char *cacert;
    const char *capath;
    const char *cert;
    const char *key;
    int connect_timeout;
    const char *header_lines[10];
    size_t header_line_count;
    const char *alpn;
    bool include_headers;
    bool insecure;
    FILE *output;
    const char *trace_file;
    Aws::Crt::LogLevel log_level;
    enum aws_http_version required_http_version;
    bool exchange_completed;
    bool bootstrap_shutdown_completed;
};

static void s_usage(int exit_code)
{

    fprintf(stderr, "usage: elasticurl [options] url\n");
    fprintf(stderr, " url: url to make a request to. The default is a GET request.\n");
    fprintf(stderr, "\n Options:\n\n");
    fprintf(stderr, "      --cacert FILE: path to a CA certficate file.\n");
    fprintf(stderr, "      --capath PATH: path to a directory containing CA files.\n");
    fprintf(stderr, "      --cert FILE: path to a PEM encoded certificate to use with mTLS\n");
    fprintf(stderr, "      --key FILE: Path to a PEM encoded private key that matches cert.\n");
    fprintf(stderr, "      --connect-timeout INT: time in milliseconds to wait for a connection.\n");
    fprintf(stderr, "  -H, --header LINE: line to send as a header in format [header-key]: [header-value]\n");
    fprintf(stderr, "  -d, --data STRING: Data to POST or PUT\n");
    fprintf(stderr, "      --data-file FILE: File to read from file and POST or PUT\n");
    fprintf(stderr, "  -M, --method STRING: Http Method verb to use for the request\n");
    fprintf(stderr, "  -G, --get: uses GET for the verb.\n");
    fprintf(stderr, "  -P, --post: uses POST for the verb.\n");
    fprintf(stderr, "  -I, --head: uses HEAD for the verb.\n");
    fprintf(stderr, "  -i, --include: includes headers in output.\n");
    fprintf(stderr, "  -k, --insecure: turns off SSL/TLS validation.\n");
    fprintf(stderr, "  -o, --output FILE: dumps content-body to FILE instead of stdout.\n");
    fprintf(stderr, "  -t, --trace FILE: dumps logs to FILE instead of stderr.\n");
    fprintf(stderr, "  -v, --verbose: ERROR|INFO|DEBUG|TRACE: log level to configure. Default is none.\n");
    fprintf(stderr, "      --version: print the version of elasticurl.\n");
    fprintf(stderr, "      --http2: HTTP/2 connection required");
    fprintf(stderr, "      --http1_1: HTTP/1.1 connection required");
    fprintf(stderr, "  -h, --help\n");
    fprintf(stderr, "            Display this message and quit.\n");
    exit(exit_code);
}

static struct aws_cli_option s_long_options[] = {
    {"cacert", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'a'},
    {"capath", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'b'},
    {"cert", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'c'},
    {"key", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'e'},
    {"connect-timeout", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'f'},
    {"header", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'H'},
    {"data", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'd'},
    {"data-file", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'g'},
    {"method", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'M'},
    {"get", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'G'},
    {"post", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'P'},
    {"head", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'I'},
    {"include", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'i'},
    {"insecure", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'k'},
    {"output", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'o'},
    {"trace", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 't'},
    {"verbose", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, NULL, 'v'},
    {"version", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'V'},
    {"http2", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'w'},
    {"http1_1", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'W'},
    {"help", AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 'h'},
    /* Per getopt(3) the last element of the array has to be filled with all zeros */
    {NULL, AWS_CLI_OPTIONS_NO_ARGUMENT, NULL, 0},
};

static void s_parse_options(int argc, char **argv, struct elasticurl_ctx *ctx)
{
    while (true)
    {
        int option_index = 0;
        int c = aws_cli_getopt_long(argc, argv, "a:b:c:e:f:H:d:g:M:GPHiko:t:v:VwWh", s_long_options, &option_index);
        if (c == -1)
        {
            break;
        }

        switch (c)
        {
            case 0:
                /* getopt_long() returns 0 if an option.flag is non-null */
                break;
            case 'a':
                ctx->cacert = aws_cli_optarg;
                break;
            case 'b':
                ctx->capath = aws_cli_optarg;
                break;
            case 'c':
                ctx->cert = aws_cli_optarg;
                break;
            case 'e':
                ctx->key = aws_cli_optarg;
                break;
            case 'f':
                ctx->connect_timeout = atoi(aws_cli_optarg);
                break;
            case 'H':
                if (ctx->header_line_count >= sizeof(ctx->header_lines) / sizeof(const char *))
                {
                    fprintf(stderr, "currently only 10 header lines are supported.\n");
                    s_usage(1);
                }
                ctx->header_lines[ctx->header_line_count++] = aws_cli_optarg;
                break;
            case 'd':
            {
                input_body = std::make_shared<std::stringstream>(aws_cli_optarg);
                break;
            }
            case 'g':
            {
                // inputfile.open(aws_cli_optarg, std::ios::in);
                // if (!inputfile.is_open())
                // {
                //     fprintf(stderr, "unable to open file %s.\n", aws_cli_optarg);
                //     s_usage(1);
                // }
                // input_body = std::make_shared<std::ifstream>(inputfile);
                // break;
            }
            case 'M':
                ctx->verb = aws_cli_optarg;
                break;
            case 'G':
                ctx->verb = "GET";
                break;
            case 'P':
                ctx->verb = "POST";
                break;
            case 'I':
                ctx->verb = "HEAD";
                break;
            case 'i':
                ctx->include_headers = true;
                break;
            case 'k':
                ctx->insecure = true;
                break;
            case 'o':
                ctx->output = fopen(aws_cli_optarg, "wb");

                if (!ctx->output)
                {
                    fprintf(stderr, "unable to open file %s.\n", aws_cli_optarg);
                    s_usage(1);
                }
                break;
            case 't':
                ctx->trace_file = aws_cli_optarg;
                break;
            case 'v':
                if (!strcmp(aws_cli_optarg, "TRACE"))
                {
                    ctx->log_level = Aws::Crt::LogLevel::Trace;
                }
                else if (!strcmp(aws_cli_optarg, "INFO"))
                {
                    ctx->log_level = Aws::Crt::LogLevel::Info;
                }
                else if (!strcmp(aws_cli_optarg, "DEBUG"))
                {
                    ctx->log_level = Aws::Crt::LogLevel::Debug;
                }
                else if (!strcmp(aws_cli_optarg, "ERROR"))
                {
                    ctx->log_level = Aws::Crt::LogLevel::Error;
                }
                else
                {
                    fprintf(stderr, "unsupported log level %s.\n", aws_cli_optarg);
                    s_usage(1);
                }
                break;
            case 'V':
                fprintf(stderr, "elasticurl %s\n", ELASTICURL_VERSION);
                exit(0);
            case 'w':
                ctx->alpn = "h2";
                ctx->required_http_version = AWS_HTTP_VERSION_2;
                break;
            case 'W':
                ctx->alpn = "http/1.1";
                ctx->required_http_version = AWS_HTTP_VERSION_1_1;
                break;
            case 'h':
                s_usage(0);
                break;
            default:
                fprintf(stderr, "Unknown option\n");
                s_usage(1);
        }
    }

    if (input_body == nullptr)
    {
        input_body = std::make_shared<std::stringstream>("");
    }

    if (aws_cli_optind < argc)
    {
        struct aws_byte_cursor uri_cursor = aws_byte_cursor_from_c_str(argv[aws_cli_optind++]);

        ctx->uri = Aws::Crt::Io::Uri(uri_cursor, ctx->allocator);
        if(ctx->uri.LastError())
        {
            fprintf(
                stderr,
                "Failed to parse uri %s with error %s\n",
                (char *)uri_cursor.ptr,
                aws_error_debug_str(ctx->uri.LastError()));
            s_usage(1);
        };
    }
    else
    {
        fprintf(stderr, "A URI for the request must be supplied.\n");
        s_usage(1);
    }
}

int main(int argc, char **argv)
{
    struct aws_allocator *allocator = aws_default_allocator();
    (void)allocator;
    (void)argc;
    (void)argv;

    struct elasticurl_ctx app_ctx;
    AWS_ZERO_STRUCT(app_ctx);
    app_ctx.allocator = allocator;
    app_ctx.connect_timeout = 3000;
    app_ctx.output = stdout;
    app_ctx.verb = "GET";
    app_ctx.alpn = "h2;http/1.1";

    s_parse_options(argc, argv, &app_ctx);

    Aws::Crt::ApiHandle apiHandle(allocator);
    if (app_ctx.trace_file)
    {
        apiHandle.InitializeLogging(app_ctx.log_level, app_ctx.trace_file);
    }
    else
    {
        apiHandle.InitializeLogging(app_ctx.log_level, stdout);
    }
    bool use_tls = true;
    uint16_t port = 443;
    if (!app_ctx.uri.GetScheme().len && (app_ctx.uri.GetPort() == 80 || app_ctx.uri.GetPort() == 8080))
    {
        use_tls = false;
    }
    else
    {
        ByteCursor scheme = app_ctx.uri.GetScheme();
        if (aws_byte_cursor_eq_c_str_ignore_case(&scheme, "http"))
        {
            use_tls = false;
        }
    }

    auto hostName = app_ctx.uri.GetHostName();

    Aws::Crt::Io::TlsContextOptions tlsCtxOptions;
    Aws::Crt::Io::TlsContext tlsContext;
    Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions;
    if (use_tls)
    {
        if (app_ctx.cert && app_ctx.key)
        {
            tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitClientWithMtls(app_ctx.cert, app_ctx.key);
        }
        else
        {
            tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();
        }
        if (app_ctx.capath || app_ctx.cacert)
        {
            tlsCtxOptions.OverrideDefaultTrustStore(app_ctx.capath, app_ctx.cacert);
        }
        if (app_ctx.insecure)
        {
            tlsCtxOptions.SetVerifyPeer(false);
        }

        tlsContext = Aws::Crt::Io::TlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);

        tlsConnectionOptions = tlsContext.NewConnectionOptions();

        tlsConnectionOptions.SetServerName(hostName);
        tlsConnectionOptions.SetAlpnList(app_ctx.alpn);
    }
    else
    {
        if (app_ctx.required_http_version == AWS_HTTP_VERSION_2)
        {
            fprintf(stderr, "Error, we don't support h2c, please use TLS for HTTP2 connection");
            exit(1);
        }
        port = 80;
        if (app_ctx.uri.GetPort())
        {
            port = app_ctx.uri.GetPort();
        }
    }

    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(app_ctx.connect_timeout);

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);

    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);

    Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);

    std::shared_ptr<Http::HttpClientConnection> connection(nullptr);
    bool errorOccured = true;
    bool connectionShutdown = false;

    std::condition_variable semaphore;
    std::mutex semaphoreLock;

    auto onConnectionSetup = [&](const std::shared_ptr<Http::HttpClientConnection> &newConnection, int errorCode) {
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);

        if (!errorCode)
        {
            connection = newConnection;
            errorOccured = false;
        }
        else
        {
            connectionShutdown = true;
        }

        semaphore.notify_one();
    };

    auto onConnectionShutdown = [&](Http::HttpClientConnection &newConnection, int errorCode) {
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);
        (void)newConnection;
        connectionShutdown = true;
        if (errorCode)
        {
            errorOccured = true;
        }

        semaphore.notify_one();
    };

    Http::HttpClientConnectionOptions httpClientConnectionOptions;
    httpClientConnectionOptions.Bootstrap = &clientBootstrap;
    httpClientConnectionOptions.OnConnectionSetupCallback = onConnectionSetup;
    httpClientConnectionOptions.OnConnectionShutdownCallback = onConnectionShutdown;
    httpClientConnectionOptions.SocketOptions = socketOptions;
    if (use_tls)
    {
        httpClientConnectionOptions.TlsOptions = tlsConnectionOptions;
    }
    httpClientConnectionOptions.HostName = String((const char *)hostName.ptr, hostName.len);
    httpClientConnectionOptions.Port = port;

    std::unique_lock<std::mutex> semaphoreULock(semaphoreLock);
    Http::HttpClientConnection::CreateConnection(httpClientConnectionOptions, allocator);
    semaphore.wait(semaphoreULock, [&]() { return connection || connectionShutdown; });

    /* Send request */
    int responseCode = 0;

    Http::HttpRequest request;
    Http::HttpRequestOptions requestOptions;
    requestOptions.request = &request;

    bool streamCompleted = false;
    requestOptions.onStreamComplete = [&](Http::HttpStream &stream, int errorCode) {
        (void)stream;
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);

        streamCompleted = true;
        if (errorCode)
        {
            errorOccured = true;
        }

        semaphore.notify_one();
    };
    requestOptions.onIncomingHeadersBlockDone = nullptr;
    requestOptions.onIncomingHeaders = [&](Http::HttpStream &stream,
                                           enum aws_http_header_block header_block,
                                           const Http::HttpHeader *header,
                                           std::size_t len) {
        /* Ignore informational headers */
        if (header_block == AWS_HTTP_HEADER_BLOCK_INFORMATIONAL)
        {
            return;
        }

        if (app_ctx.include_headers)
        {
            if (!app_ctx.response_code_written)
            {
                responseCode = stream.GetResponseStatusCode();
                fprintf(stdout, "Response Status: %d\n", responseCode);
                app_ctx.response_code_written = true;
            }

            for (size_t i = 0; i < len; ++i)
            {
                fwrite(header[i].name.ptr, 1, header[i].name.len, stdout);
                fprintf(stdout, ": ");
                fwrite(header[i].value.ptr, 1, header[i].value.len, stdout);
                fprintf(stdout, "\n");
            }
        }
    };
    requestOptions.onIncomingBody = [&](Http::HttpStream &, const ByteCursor &data) {
        fwrite(data.ptr, 1, data.len, app_ctx.output);
    };

    request.SetMethod(ByteCursorFromCString(app_ctx.verb));
    request.SetPath(app_ctx.uri.GetPathAndQuery());

    Http::HttpHeader host_header;
    host_header.name = ByteCursorFromCString("host");
    host_header.value = app_ctx.uri.GetHostName();
    request.AddHeader(host_header);

    Http::HttpHeader user_agent_header;
    user_agent_header.name = ByteCursorFromCString("user-agent");
    user_agent_header.value = ByteCursorFromCString("elasticurl_cpp 1.0, Powered by the AWS Common Runtime.");
    request.AddHeader(user_agent_header);

    std::shared_ptr<Aws::Crt::Io::StdIOStreamInputStream> bodyStream =
        MakeShared<Io::StdIOStreamInputStream>(allocator, input_body, allocator);
    int64_t data_len = 0;
    if (aws_input_stream_get_length(bodyStream->GetUnderlyingStream(), &data_len))
    {
        fprintf(stderr, "failed to get length of input stream.\n");
        exit(1);
    }
    if (data_len > 0)
    {
        std::string content_length = std::to_string(data_len);
        Http::HttpHeader content_length_header;
        content_length_header.name = ByteCursorFromCString("content-length");
        content_length_header.value = ByteCursorFromCString(content_length.c_str());
        request.AddHeader(content_length_header);
        request.SetBody(bodyStream);
    }

    for (size_t i = 0; i < app_ctx.header_line_count; ++i)
    {
        char *delimiter = (char *)memchr(app_ctx.header_lines[i], ':', strlen(app_ctx.header_lines[i]));

        if (!delimiter)
        {
            fprintf(stderr, "invalid header line %s configured.", app_ctx.header_lines[i]);
            exit(1);
        }

        Http::HttpHeader user_header;
        user_header.name = ByteCursorFromArray((uint8_t *)app_ctx.header_lines[i], delimiter - app_ctx.header_lines[i]);
        user_header.value = ByteCursorFromCString(delimiter + 1);
        request.AddHeader(user_header);
    }

    auto stream = connection->NewClientStream(requestOptions);
    stream->Activate();

    semaphore.wait(semaphoreULock, [&]() { return streamCompleted; });

    connection->Close();
    semaphore.wait(semaphoreULock, [&]() { return connectionShutdown; });

    /* TODO: Wait until the bootstrap finishing shutting down, we may need to break the API for create the Bootstrap, a
     * Bootstrap option for the callback? */

    if (app_ctx.output != stdout)
    {
        fclose(app_ctx.output);
    }

    if (inputfile.is_open())
    {
        inputfile.close();
    }

    return 0;
}
