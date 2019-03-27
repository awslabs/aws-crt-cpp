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
    std::ifstream file1(fileName1);
    std::ifstream file2(fileName2);

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

    ByteBuf file1DigestBuf = ByteBufFromArray(file1Digest, sizeof(file1Digest));
    file1DigestBuf.len = 0;
    ByteBuf file2DigestBuf = ByteBufFromArray(file2Digest, sizeof(file2Digest));
    file2DigestBuf.len = 0;

    ASSERT_TRUE(file1Hash.Digest(file1DigestBuf));
    ASSERT_TRUE(file2Hash.Digest(file2DigestBuf));

    return aws_byte_buf_eq(&file1DigestBuf, &file2DigestBuf) ? AWS_OP_SUCCESS : AWS_OP_ERR;
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
    AWS_ZERO_STRUCT(socketOptions);
    socketOptions.type = AWS_SOCKET_STREAM;
    socketOptions.domain = AWS_SOCKET_IPV4;
    socketOptions.connect_timeout_ms = 3000;

    Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
    ASSERT_TRUE(eventLoopGroup);

    Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, allocator);
    ASSERT_TRUE(allocator);

    Http::HttpClient httpClient(&clientBootstrap, SIZE_MAX, allocator);

    std::shared_ptr<Http::HttpConnection> connection(nullptr);
    bool errorOccured = true;
    bool connectionShutdown = false;

    std::condition_variable semaphore;
    std::mutex semaphoreLock;

    auto onConnectionSetup = [&](const std::shared_ptr<Http::HttpConnection> &newConnection, int errorCode) {
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

    auto onConnectionShutdown = [&](const std::shared_ptr<Http::HttpConnection> &newConnection, int errorCode) {
        std::lock_guard<std::mutex> lockGuard(semaphoreLock);

        connectionShutdown = true;
        if (errorCode)
        {
            errorOccured = true;
        }

        semaphore.notify_one();
    };

    httpClient.onConnectionSetup = onConnectionSetup;
    httpClient.onConnectionShutdown = onConnectionShutdown;

    std::unique_lock<std::mutex> semaphoreULock(semaphoreLock);
    ASSERT_TRUE(httpClient.NewConnection(hostName, 443, socketOptions, tlsConnectionOptions));
    semaphore.wait(semaphoreULock, [&]() { return connection || connectionShutdown; });

    ASSERT_FALSE(errorOccured);
    ASSERT_FALSE(connectionShutdown);
    ASSERT_TRUE(connection);

    int responseCode = 0;
    std::ofstream downloadedFile("http_download_test_file.txt");
    ASSERT_TRUE(downloadedFile);

    Http::HttpRequestOptions requestOptions;
    requestOptions.onStreamOutgoingBody = nullptr;

    bool streamCompleted = false;
    requestOptions.onStreamComplete = [&](const std::shared_ptr<Http::HttpStream> &stream, int errorCode) {
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
        [&](const std::shared_ptr<Http::HttpStream> &stream, const struct aws_http_header *header, std::size_t len) {
            responseCode = stream->GetIncommingResponseStatusCode();
        };
    requestOptions.onIncomingBody =
        [&](const std::shared_ptr<Http::HttpStream> &, const ByteCursor &data, std::size_t &outWindowUpdateSize) {
            downloadedFile.write((const char *)data.ptr, data.len);
        };

    requestOptions.method = ByteCursorFromCString("GET");
    requestOptions.uri = uri.GetPathAndQuery();

    aws_http_header host_header;
    host_header.name_str = ByteCursorFromCString("host");
    host_header.value = uri.GetHostName();
    requestOptions.headerArray = &host_header;
    requestOptions.headerArrayLength = 1;

    connection->NewStream(requestOptions);
    semaphore.wait(semaphoreULock, [&]() { return streamCompleted; });
    ASSERT_INT_EQUALS(200, responseCode);

    connection->Close();
    semaphore.wait(semaphoreULock, [&]() { return connectionShutdown; });

    return s_VerifyFilesAreTheSame(allocator, "http_download_test_file.txt", "http_test_doc.txt");
}

AWS_TEST_CASE(HttpDownloadNoBackPressure, s_TestHttpDownloadNoBackPressure)
