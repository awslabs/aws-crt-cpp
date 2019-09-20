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

#include <aws/io/uri.h>

#include <aws/testing/aws_test_harness.h>

#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>

using namespace Aws::Crt;

static int s_VerifyFilesAreTheSame(Allocator *allocator, const char *fileName1, const char *fileName2)
{
    std::ifstream file1(fileName1, std::ios_base::binary);
    std::ifstream file2(fileName2, std::ios_base::binary);

    ASSERT_TRUE(file1);
    ASSERT_TRUE(file2);

    auto file1Hash = Crypto::Hash::CreateSHA256(allocator);
    uint8_t buffer[1024];
    AWS_ZERO_ARRAY(buffer);

    while (file1.read((char *)buffer, sizeof(buffer)))
    {
        auto read = file1.gcount();
        ByteCursor toHash = ByteCursorFromArray(buffer, (size_t)read);
        ASSERT_TRUE(file1Hash.Update(toHash));
    }

    auto file2Hash = Crypto::Hash::CreateSHA256(allocator);

    while (file2.read((char *)buffer, sizeof(buffer)))
    {
        auto read = file2.gcount();
        ByteCursor toHash = ByteCursorFromArray(buffer, (size_t)read);
        ASSERT_TRUE(file2Hash.Update(toHash));
    }

    uint8_t file1Digest[Crypto::SHA256_DIGEST_SIZE];
    AWS_ZERO_ARRAY(file1Digest);
    uint8_t file2Digest[Crypto::SHA256_DIGEST_SIZE];
    AWS_ZERO_ARRAY(file2Digest);

    ByteBuf file1DigestBuf = ByteBufFromEmptyArray(file1Digest, sizeof(file1Digest));
    ByteBuf file2DigestBuf = ByteBufFromEmptyArray(file2Digest, sizeof(file2Digest));

    ASSERT_TRUE(file1Hash.Digest(file1DigestBuf));
    ASSERT_TRUE(file2Hash.Digest(file2DigestBuf));

    ASSERT_BIN_ARRAYS_EQUALS(file2DigestBuf.buffer, file2DigestBuf.len, file1DigestBuf.buffer, file1DigestBuf.len);
    return AWS_OP_SUCCESS;
}

static int s_TestHttpDownloadNoBackPressure(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);
    Aws::Crt::Io::TlsContextOptions tlsCtxOptions = Aws::Crt::Io::TlsContextOptions::InitDefaultClient();
    Aws::Crt::Io::TlsContext tlsContext(tlsCtxOptions, Aws::Crt::Io::TlsMode::CLIENT, allocator);
    ASSERT_TRUE(tlsContext);

    Aws::Crt::Io::TlsConnectionOptions tlsConnectionOptions = tlsContext.NewConnectionOptions();

    ByteCursor cursor = ByteCursorFromCString("https://aws-crt-test-stuff.s3.amazonaws.com/http_test_doc.txt");
    Io::Uri uri(cursor, allocator);

    auto hostName = uri.GetHostName();
    tlsConnectionOptions.SetServerName(hostName);

    Aws::Crt::Io::SocketOptions socketOptions;
    socketOptions.SetConnectTimeoutMs(1000);

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
    ASSERT_TRUE(eventLoopGroup);

    Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
    ASSERT_TRUE(defaultHostResolver);

    Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
    ASSERT_TRUE(clientBootstrap);

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
    httpClientConnectionOptions.TlsOptions = tlsConnectionOptions;
    httpClientConnectionOptions.HostName = String((const char *)hostName.ptr, hostName.len);
    httpClientConnectionOptions.Port = 443;

    std::unique_lock<std::mutex> semaphoreULock(semaphoreLock);
    ASSERT_TRUE(Http::HttpClientConnection::CreateConnection(httpClientConnectionOptions, allocator));
    semaphore.wait(semaphoreULock, [&]() { return connection || connectionShutdown; });

    ASSERT_FALSE(errorOccured);
    ASSERT_FALSE(connectionShutdown);
    ASSERT_TRUE(connection);

    int responseCode = 0;
    std::ofstream downloadedFile("http_download_test_file.txt", std::ios_base::binary);
    ASSERT_TRUE(downloadedFile);

    Http::HttpRequest request;
    Http::HttpRequestOptions requestOptions;
    requestOptions.request = &request;

    bool streamCompleted = false;
    requestOptions.onStreamComplete = [&](Http::HttpStream &stream, int errorCode) {
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);

        streamCompleted = true;
        if (errorCode)
        {
            errorOccured = true;
        }

        semaphore.notify_one();
    };
    requestOptions.onIncomingHeadersBlockDone = nullptr;
    requestOptions.onIncomingHeaders =
        [&](Http::HttpStream &stream, enum aws_http_header_block, const Http::HttpHeader *header, std::size_t len) {
            responseCode = stream.GetResponseStatusCode();
        };
    requestOptions.onIncomingBody = [&](Http::HttpStream &, const ByteCursor &data) {
        downloadedFile.write((const char *)data.ptr, data.len);
    };

    request.SetMethod(ByteCursorFromCString("GET"));
    request.SetPath(uri.GetPathAndQuery());

    Http::HttpHeader host_header;
    host_header.name = ByteCursorFromCString("host");
    host_header.value = uri.GetHostName();
    request.AddHeader(host_header);

    connection->NewClientStream(requestOptions);
    semaphore.wait(semaphoreULock, [&]() { return streamCompleted; });
    ASSERT_INT_EQUALS(200, responseCode);

    connection->Close();
    semaphore.wait(semaphoreULock, [&]() { return connectionShutdown; });

    downloadedFile.flush();
    downloadedFile.close();
    return s_VerifyFilesAreTheSame(allocator, "http_download_test_file.txt", "http_test_doc.txt");
}

AWS_TEST_CASE(HttpDownloadNoBackPressure, s_TestHttpDownloadNoBackPressure)
