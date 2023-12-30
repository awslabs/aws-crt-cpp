/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/crypto/Hash.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/Uri.h>

#include <aws/common/command_line_parser.h>
#include <condition_variable>
#include <fstream>
#include <future>
#include <iostream>

using namespace Aws::Crt;

#define ELASTICURL_VERSION "0.0.1"

struct ElasticurlCtx
{
    Allocator *allocator = nullptr;
    const char *verb = "GET";
    Io::Uri uri;
    bool ResponseCodeWritten = false;
    const char *CaCert = nullptr;
    const char *CaPath = nullptr;
    const char *Cert = nullptr;
    const char *Key = nullptr;
    int ConnectTimeout = 3000;
    Vector<const char *> HeaderLines;
    const char *Alpn = "h2;http/1.1";
    bool IncludeHeaders = false;
    bool Insecure = false;

    const char *TraceFile = nullptr;
    Aws::Crt::LogLevel LogLevel = Aws::Crt::LogLevel::None;
    Http::HttpVersion RequiredHttpVersion = Http::HttpVersion::Unknown;

    std::shared_ptr<Io::IStream> InputBody = nullptr;
    std::ofstream Output;
};

static void s_Usage(int exit_code)
{

    std::cerr << "usage: elasticurl [options] url\n";
    std::cerr << " url: url to make a request to. The default is a GET request.\n";
    std::cerr << "\n Options:\n\n";
    std::cerr << "      --cacert FILE: path to a CA certficate file.\n";
    std::cerr << "      --capath PATH: path to a directory containing CA files.\n";
    std::cerr << "      --cert FILE: path to a PEM encoded certificate to use with mTLS\n";
    std::cerr << "      --key FILE: Path to a PEM encoded private key that matches cert.\n";
    std::cerr << "      --connect-timeout INT: time in milliseconds to wait for a connection.\n";
    std::cerr << "  -H, --header LINE: line to send as a header in format [header-key]: [header-value]\n";
    std::cerr << "  -d, --data STRING: Data to POST or PUT\n";
    std::cerr << "      --data-file FILE: File to read from file and POST or PUT\n";
    std::cerr << "  -M, --method STRING: Http Method verb to use for the request\n";
    std::cerr << "  -G, --get: uses GET for the verb.\n";
    std::cerr << "  -P, --post: uses POST for the verb.\n";
    std::cerr << "  -I, --head: uses HEAD for the verb.\n";
    std::cerr << "  -i, --include: includes headers in output.\n";
    std::cerr << "  -k, --insecure: turns off SSL/TLS validation.\n";
    std::cerr << "  -o, --output FILE: dumps content-body to FILE instead of stdout.\n";
    std::cerr << "  -t, --trace FILE: dumps logs to FILE instead of stderr.\n";
    std::cerr << "  -v, --verbose: ERROR|INFO|DEBUG|TRACE: log level to configure. Default is none.\n";
    std::cerr << "      --version: print the version of elasticurl.\n";
    std::cerr << "      --http2: HTTP/2 connection required\n";
    std::cerr << "      --http1_1: HTTP/1.1 connection required\n";
    std::cerr << "  -h, --help\n";
    std::cerr << "            Display this message and quit.\n";
    exit(exit_code);
}

static struct aws_cli_option s_LongOptions[] = {
    {"cacert", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, nullptr, 'a'},
    {"capath", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, nullptr, 'b'},
    {"cert", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, nullptr, 'c'},
    {"key", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, nullptr, 'e'},
    {"connect-timeout", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, nullptr, 'f'},
    {"header", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, nullptr, 'H'},
    {"data", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, nullptr, 'd'},
    {"data-file", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, nullptr, 'g'},
    {"method", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, nullptr, 'M'},
    {"get", AWS_CLI_OPTIONS_NO_ARGUMENT, nullptr, 'G'},
    {"post", AWS_CLI_OPTIONS_NO_ARGUMENT, nullptr, 'P'},
    {"head", AWS_CLI_OPTIONS_NO_ARGUMENT, nullptr, 'I'},
    {"include", AWS_CLI_OPTIONS_NO_ARGUMENT, nullptr, 'i'},
    {"insecure", AWS_CLI_OPTIONS_NO_ARGUMENT, nullptr, 'k'},
    {"output", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, nullptr, 'o'},
    {"trace", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, nullptr, 't'},
    {"verbose", AWS_CLI_OPTIONS_REQUIRED_ARGUMENT, nullptr, 'v'},
    {"version", AWS_CLI_OPTIONS_NO_ARGUMENT, nullptr, 'V'},
    {"http2", AWS_CLI_OPTIONS_NO_ARGUMENT, nullptr, 'w'},
    {"http1_1", AWS_CLI_OPTIONS_NO_ARGUMENT, nullptr, 'W'},
    {"help", AWS_CLI_OPTIONS_NO_ARGUMENT, nullptr, 'h'},
    /* Per getopt(3) the last element of the array has to be filled with all zeros */
    {nullptr, AWS_CLI_OPTIONS_NO_ARGUMENT, nullptr, 0},
};

static void s_ParseOptions(int argc, char **argv, ElasticurlCtx &ctx)
{
    while (true)
    {
        int option_index = 0;
        int c = aws_cli_getopt_long(argc, argv, "a:b:c:e:f:H:d:g:M:GPHiko:t:v:VwWh", s_LongOptions, &option_index);
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
            case 2:
                /* getopt_long() returns 0x02 (START_OF_TEXT) if a positional arg was encountered */
                ctx.uri = Io::Uri(aws_byte_cursor_from_c_str(aws_cli_positional_arg), ctx.allocator);
                if (!ctx.uri)
                {
                    std::cerr << "Failed to parse uri \"" << aws_cli_positional_arg << "\" with error "
                              << aws_error_debug_str(ctx.uri.LastError()) << std::endl;
                    s_Usage(1);
                }
                break;
            case 'a':
                ctx.CaCert = aws_cli_optarg;
                break;
            case 'b':
                ctx.CaPath = aws_cli_optarg;
                break;
            case 'c':
                ctx.Cert = aws_cli_optarg;
                break;
            case 'e':
                ctx.Key = aws_cli_optarg;
                break;
            case 'f':
                ctx.ConnectTimeout = atoi(aws_cli_optarg);
                break;
            case 'H':
                ctx.HeaderLines.push_back(aws_cli_optarg);
                break;
            case 'd':
            {
                ctx.InputBody = std::make_shared<std::stringstream>(aws_cli_optarg);
                break;
            }
            case 'g':
            {
                ctx.InputBody = std::make_shared<std::ifstream>(aws_cli_optarg, std::ios::in);
                if (!ctx.InputBody->good())
                {
                    std::cerr << "unable to open file " << aws_cli_optarg << std::endl;
                    s_Usage(1);
                }
                break;
            }
            case 'M':
                ctx.verb = aws_cli_optarg;
                break;
            case 'G':
                ctx.verb = "GET";
                break;
            case 'P':
                ctx.verb = "POST";
                break;
            case 'I':
                ctx.verb = "HEAD";
                break;
            case 'i':
                ctx.IncludeHeaders = true;
                break;
            case 'k':
                ctx.Insecure = true;
                break;
            case 'o':
                ctx.Output.open(aws_cli_optarg, std::ios::out | std::ios::binary);
                break;
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
            case 'V':
                std::cerr << "elasticurl " << ELASTICURL_VERSION << std::endl;
                exit(0);
            case 'w':
                ctx.Alpn = "h2";
                ctx.RequiredHttpVersion = Http::HttpVersion::Http2;
                break;
            case 'W':
                ctx.Alpn = "http/1.1";
                ctx.RequiredHttpVersion = Http::HttpVersion::Http1_1;
                break;
            case 'h':
                s_Usage(0);
                break;
            default:
                std::cerr << "Unknown option\n";
                s_Usage(1);
        }
    }

    if (ctx.InputBody == nullptr)
    {
        ctx.InputBody = std::make_shared<std::stringstream>("");
    }

    if (!ctx.uri)
    {
        std::cerr << "A URI for the request must be supplied.\n";
        s_Usage(1);
    }
}

int main(int argc, char **argv)
{
    struct aws_allocator *allocator = aws_default_allocator();
    ApiHandle apiHandle(allocator);

    struct ElasticurlCtx appCtx;
    appCtx.allocator = allocator;

    s_ParseOptions(argc, argv, appCtx);
    if (appCtx.TraceFile)
    {
        apiHandle.InitializeLogging(appCtx.LogLevel, appCtx.TraceFile);
    }
    else
    {
        apiHandle.InitializeLogging(appCtx.LogLevel, stderr);
    }
    bool useTls = true;
    uint32_t port = 443;
    if (!appCtx.uri.GetScheme().len && (appCtx.uri.GetPort() == 80 || appCtx.uri.GetPort() == 8080))
    {
        useTls = false;
    }
    else
    {
        ByteCursor scheme = appCtx.uri.GetScheme();
        if (aws_byte_cursor_eq_c_str_ignore_case(&scheme, "http"))
        {
            useTls = false;
        }
    }

    auto hostName = appCtx.uri.GetHostName();

    Io::TlsContextOptions tlsCtxOptions;
    Io::TlsContext tlsContext;
    Io::TlsConnectionOptions tlsConnectionOptions;
    if (useTls)
    {
        if (appCtx.Cert && appCtx.Key)
        {
            tlsCtxOptions = Io::TlsContextOptions::InitClientWithMtls(appCtx.Cert, appCtx.Key);
            if (!tlsCtxOptions)
            {
                std::cerr << "Failed to load " << appCtx.Cert << " and " << appCtx.Key << " with error "
                          << aws_error_debug_str(tlsCtxOptions.LastError()) << std::endl;
                exit(1);
            }
        }
        else
        {
            tlsCtxOptions = Io::TlsContextOptions::InitDefaultClient();
            if (!tlsCtxOptions)
            {
                std::cerr << "Failed to create a default tlsCtxOptions with error "
                          << aws_error_debug_str(tlsCtxOptions.LastError()) << std::endl;
                exit(1);
            }
        }

        if (appCtx.CaPath || appCtx.CaCert)
        {
            if (!tlsCtxOptions.OverrideDefaultTrustStore(appCtx.CaPath, appCtx.CaCert))
            {
                std::cerr << "Failed to load " << appCtx.CaPath << " and " << appCtx.CaCert << " with error "
                          << aws_error_debug_str(tlsCtxOptions.LastError()) << std::endl;
                exit(1);
            }
        }
        if (appCtx.Insecure)
        {
            tlsCtxOptions.SetVerifyPeer(false);
        }

        tlsContext = Io::TlsContext(tlsCtxOptions, Io::TlsMode::CLIENT, allocator);

        tlsConnectionOptions = tlsContext.NewConnectionOptions();

        if (!tlsConnectionOptions.SetServerName(hostName))
        {
            std::cerr << "Failed to set servername with error " << aws_error_debug_str(tlsConnectionOptions.LastError())
                      << std::endl;
            exit(1);
        }
        if (!tlsConnectionOptions.SetAlpnList(appCtx.Alpn))
        {
            std::cerr << "Failed to load alpn list with error " << aws_error_debug_str(tlsConnectionOptions.LastError())
                      << std::endl;
            exit(1);
        }
    }
    else
    {
        if (appCtx.RequiredHttpVersion == Http::HttpVersion::Http2)
        {
            std::cerr << "Error, we don't support h2c, please use TLS for HTTP/2 connection" << std::endl;
            exit(1);
        }
        port = 80;
        if (appCtx.uri.GetPort())
        {
            port = appCtx.uri.GetPort();
        }
    }

    Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(appCtx.ConnectTimeout);

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

    std::promise<std::shared_ptr<Http::HttpClientConnection>> connectionPromise;
    std::promise<void> shutdownPromise;

    auto onConnectionSetup =
        [&appCtx, &connectionPromise](const std::shared_ptr<Http::HttpClientConnection> &newConnection, int errorCode) {
            if (!errorCode)
            {
                if (appCtx.RequiredHttpVersion != Http::HttpVersion::Unknown)
                {
                    if (newConnection->GetVersion() != appCtx.RequiredHttpVersion)
                    {
                        std::cerr << "Error. The requested HTTP version, " << appCtx.Alpn
                                  << ", is not supported by the peer." << std::endl;
                        exit(1);
                    }
                }
            }
            else
            {
                std::cerr << "Connection failed with error " << aws_error_debug_str(errorCode) << std::endl;
                exit(1);
            }
            connectionPromise.set_value(newConnection);
        };

    auto onConnectionShutdown = [&shutdownPromise](Http::HttpClientConnection &newConnection, int errorCode) {
        (void)newConnection;
        if (errorCode)
        {
            std::cerr << "Connection shutdown with error " << aws_error_debug_str(errorCode) << std::endl;
            exit(1);
        }

        shutdownPromise.set_value();
    };

    Http::HttpClientConnectionOptions httpClientConnectionOptions;
    httpClientConnectionOptions.Bootstrap = &clientBootstrap;
    httpClientConnectionOptions.OnConnectionSetupCallback = onConnectionSetup;
    httpClientConnectionOptions.OnConnectionShutdownCallback = onConnectionShutdown;
    httpClientConnectionOptions.SocketOptions = socketOptions;
    if (useTls)
    {
        httpClientConnectionOptions.TlsOptions = tlsConnectionOptions;
    }
    httpClientConnectionOptions.HostName = String((const char *)hostName.ptr, hostName.len);
    httpClientConnectionOptions.Port = port;

    Http::HttpClientConnection::CreateConnection(httpClientConnectionOptions, allocator);

    std::shared_ptr<Http::HttpClientConnection> connection = connectionPromise.get_future().get();
    /* Send request */
    int responseCode = 0;

    Http::HttpRequest request;
    Http::HttpRequestOptions requestOptions;
    requestOptions.request = &request;
    std::promise<void> streamCompletePromise;

    requestOptions.onStreamComplete = [&streamCompletePromise](Http::HttpStream &stream, int errorCode) {
        (void)stream;
        if (errorCode)
        {
            std::cerr << "Stream completed with error " << aws_error_debug_str(errorCode) << std::endl;
            exit(1);
        }
        streamCompletePromise.set_value();
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

        if (appCtx.IncludeHeaders)
        {
            if (!appCtx.ResponseCodeWritten)
            {
                responseCode = stream.GetResponseStatusCode();
                std::cout << "Response Status: " << responseCode << std::endl;
                appCtx.ResponseCodeWritten = true;
            }

            for (size_t i = 0; i < len; ++i)
            {
                std::cout.write((char *)header[i].name.ptr, header[i].name.len);
                std::cout << ": ";
                std::cout.write((char *)header[i].value.ptr, header[i].value.len);
                std::cout << std::endl;
            }
        }
    };
    requestOptions.onIncomingBody = [&appCtx](Http::HttpStream &, const ByteCursor &data) {
        if (appCtx.Output.is_open())
        {
            appCtx.Output.write((char *)data.ptr, data.len);
        }
        else
        {
            std::cout.write((char *)data.ptr, data.len);
        }
    };

    request.SetMethod(ByteCursorFromCString(appCtx.verb));
    auto pathAndQuery = appCtx.uri.GetPathAndQuery();
    if (pathAndQuery.len > 0) {
        request.SetPath(pathAndQuery);
    } else {
        request.SetPath(ByteCursorFromCString("/"));
    }

    if (connection->GetVersion() == Http::HttpVersion::Http2)
    {
        Http::HttpHeader authHeader;
        authHeader.name = ByteCursorFromCString(":authority");
        authHeader.value = appCtx.uri.GetHostName();
        request.AddHeader(authHeader);
    }
    else
    {
        Http::HttpHeader hostHeader;
        hostHeader.name = ByteCursorFromCString("host");
        hostHeader.value = appCtx.uri.GetHostName();
        request.AddHeader(hostHeader);
    }

    Http::HttpHeader userAgentHeader;
    userAgentHeader.name = ByteCursorFromCString("user-agent");
    userAgentHeader.value = ByteCursorFromCString("elasticurl_cpp 1.0, Powered by the AWS Common Runtime.");
    request.AddHeader(userAgentHeader);

    std::shared_ptr<Io::StdIOStreamInputStream> bodyStream =
        MakeShared<Io::StdIOStreamInputStream>(allocator, appCtx.InputBody, allocator);
    int64_t dataLen;
    if (!bodyStream->GetLength(dataLen))
    {
        std::cerr << "failed to get length of input stream.\n";
        exit(1);
    }
    if (dataLen > 0)
    {
        std::string contentLength = std::to_string(dataLen);
        Http::HttpHeader contentLengthHeader;
        contentLengthHeader.name = ByteCursorFromCString("content-length");
        contentLengthHeader.value = ByteCursorFromCString(contentLength.c_str());
        request.AddHeader(contentLengthHeader);
        request.SetBody(bodyStream);
    }

    for (auto headerLine : appCtx.HeaderLines)
    {
        char *delimiter = (char *)memchr(headerLine, ':', strlen(headerLine));

        if (!delimiter)
        {
            std::cerr << "invalid header line " << headerLine << " configured." << std::endl;
            exit(1);
        }

        Http::HttpHeader userHeader;
        userHeader.name = ByteCursorFromArray((uint8_t *)headerLine, delimiter - headerLine);
        userHeader.value = ByteCursorFromCString(delimiter + 1);
        request.AddHeader(userHeader);
    }

    auto stream = connection->NewClientStream(requestOptions);
    stream->Activate();

    streamCompletePromise.get_future().wait(); // wait for connection shutdown to complete

    connection->Close();
    shutdownPromise.get_future().wait(); // wait for connection shutdown to complete

    return 0;
}
