/*
 * Copyright 2010-2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
#include "S3ObjectTransport.h"
#include "CanaryApp.h"
#include "MetricsPublisher.h"

#include <aws/common/thread.h>
#include <aws/crt/Api.h>
#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/EndPointMonitor.h>
#include <aws/crt/io/Stream.h>
#include <aws/io/stream.h>
#include <inttypes.h>
#include <iostream>

#if defined(_WIN32)
#    undef min
#endif

using namespace Aws::Crt;

namespace
{
    const int32_t S3GetObjectResponseStatus_PartialContent = 206;
    const uint32_t ConnectionMonitoringFailureIntervalSeconds = 1;
    const bool ConnectionMonitoringEnabled = false;
    const bool EndPointMonitoringEnabled = false;
} // namespace

S3ObjectTransport::S3ObjectTransport(
    CanaryApp &canaryApp,
    const Aws::Crt::String &bucket,
    uint32_t maxConnections,
    uint64_t minThroughputBytesPerSecond)
    : m_canaryApp(canaryApp), m_bucketName(bucket), m_transfersPerAddress(10), m_activeRequestsCount(0)
{
    m_endpoint = m_bucketName + ".s3." + m_canaryApp.GetOptions().region.c_str() + ".amazonaws.com";

    m_hostHeader.name = ByteCursorFromCString("host");
    m_hostHeader.value = ByteCursorFromCString(m_endpoint.c_str());

    m_contentTypeHeader.name = ByteCursorFromCString("content-type");
    m_contentTypeHeader.value = ByteCursorFromCString("text/plain");

    m_minThroughputBytes = minThroughputBytesPerSecond;

    Http::HttpClientConnectionManagerOptions connectionManagerOptions;

    connectionManagerOptions.ConnectionOptions.HostName = m_endpoint;
    connectionManagerOptions.ConnectionOptions.Port = m_canaryApp.GetOptions().sendEncrypted ? 443 : 80;
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetConnectTimeoutMs(3000);
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetSocketType(AWS_SOCKET_STREAM);
    connectionManagerOptions.ConnectionOptions.InitialWindowSize = SIZE_MAX;

    if (m_canaryApp.GetOptions().sendEncrypted)
    {
        aws_byte_cursor serverName = ByteCursorFromCString(m_endpoint.c_str());
        auto connOptions = m_canaryApp.GetTlsContext().NewConnectionOptions();
        connOptions.SetServerName(serverName);
        connectionManagerOptions.ConnectionOptions.TlsOptions = connOptions;
    }

    if (m_minThroughputBytes > 0)
    {
        if (EndPointMonitoringEnabled)
        {
            AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Endpoint monitoring enabled.");

            Io::EndPointMonitorOptions options;
            options.m_expectedPerSampleThroughput = m_minThroughputBytes;
            options.m_allowedFailureInterval = 2ULL;
            options.m_schedulingLoop =
                aws_event_loop_group_get_next_loop(canaryApp.GetEventLoopGroup().GetUnderlyingHandle());
            options.m_hostResolver = canaryApp.GetDefaultHostResolver().GetUnderlyingHandle();
            options.m_endPoint = m_endpoint;

            m_endPointMonitorManager =
                MakeShared<Io::EndPointMonitorManager>(g_allocator, options); // TODO use unique pointer

            connectionManagerOptions.OnConnectionCreated = [this](struct aws_http_connection *connection) {
                if (m_endPointMonitorManager != nullptr)
                {
                    m_endPointMonitorManager->AttachMonitor(connection);
                }
            };
        }

        if (ConnectionMonitoringEnabled)
        {
            AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Connection monitoring enabled.");

            Http::HttpConnectionMonitoringOptions monitoringOptions;
            monitoringOptions.allowable_throughput_failure_interval_seconds =
                ConnectionMonitoringFailureIntervalSeconds;
            monitoringOptions.minimum_throughput_bytes_per_second = m_minThroughputBytes;

            connectionManagerOptions.ConnectionOptions.MonitoringOptions = monitoringOptions;
        }
    }

    connectionManagerOptions.ConnectionOptions.Bootstrap = &m_canaryApp.GetBootstrap();
    connectionManagerOptions.MaxConnections = std::max(1U, maxConnections);

    m_connManager =
        Http::HttpClientConnectionManager::NewClientConnectionManager(connectionManagerOptions, g_allocator);
}

void S3ObjectTransport::WarmDNSCache(uint32_t numTransfers, uint32_t transfersPerAddress)
{
    if (m_endPointMonitorManager != nullptr)
    {
        m_endPointMonitorManager->SetupCallbacks();
    }

    m_transfersPerAddress = transfersPerAddress;

    // Each transfer is in a group the size of TransfersPerAddress,
    size_t desiredNumberOfAddresses = numTransfers / m_transfersPerAddress;

    if ((numTransfers % m_transfersPerAddress) > 0)
    {
        ++desiredNumberOfAddresses;
    }

    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY,
        "Warming DNS cache: getting %" PRIu64 " addresses for endpoint %s",
        (uint64_t)desiredNumberOfAddresses,
        m_endpoint.c_str());

    aws_host_resolver_purge_cache(m_canaryApp.GetDefaultHostResolver().GetUnderlyingHandle());

    // Ask the host resolver to start resolving.
    m_canaryApp.GetDefaultHostResolver().ResolveHost(
        m_endpoint, [](Io::HostResolver &, const Vector<Io::HostAddress> &, int) {});

    // Wait until the resolved address count is what we need it to be.
    {
        size_t numAddresses = m_canaryApp.GetDefaultHostResolver().GetHostAddressCount(
            m_endpoint, AWS_GET_HOST_ADDRESS_COUNT_RECORD_TYPE_A);

        while (numAddresses < desiredNumberOfAddresses)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            numAddresses = m_canaryApp.GetDefaultHostResolver().GetHostAddressCount(
                m_endpoint, AWS_GET_HOST_ADDRESS_COUNT_RECORD_TYPE_A);
        }
    }

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "DNS cache warmed.");
}

void S3ObjectTransport::MakeSignedRequest(
    const std::shared_ptr<Aws::Crt::Http::HttpClientConnection> &connection,
    const std::shared_ptr<Http::HttpRequest> &request,
    const Http::HttpRequestOptions &requestOptions,
    SignedRequestCallback callback)
{
    String region = m_canaryApp.GetOptions().region.c_str();

    Auth::AwsSigningConfig signingConfig(g_allocator);
    signingConfig.SetRegion(region);
    signingConfig.SetCredentialsProvider(m_canaryApp.GetCredsProvider());
    signingConfig.SetService("s3");
    signingConfig.SetBodySigningType(Auth::BodySigningType::UnsignedPayload);
    signingConfig.SetSigningTimepoint(DateTime::Now());
    signingConfig.SetSigningAlgorithm(Auth::SigningAlgorithm::SigV4Header);

    m_canaryApp.GetSigner()->SignRequest(
        request,
        signingConfig,
        [this, connection, requestOptions, callback](
            const std::shared_ptr<Aws::Crt::Http::HttpRequest> &signedRequest, int signingErrorCode) {
            if (signingErrorCode != AWS_ERROR_SUCCESS)
            {
                if (callback != nullptr)
                {
                    callback(nullptr, signingErrorCode);
                }
                return;
            }

            if (connection != nullptr)
            {
                if (!connection->IsOpen())
                {
                    if (callback != nullptr)
                    {
                        callback(nullptr, AWS_ERROR_UNKNOWN);
                    }
                }
                else
                {
                    if (callback != nullptr)
                    {
                        callback(connection, AWS_ERROR_SUCCESS);
                    }

                    MakeSignedRequest_SendRequest(connection, requestOptions, signedRequest);
                }
            }
            else
            {
                m_connManager->AcquireConnection(
                    [this, requestOptions, signedRequest, callback](
                        std::shared_ptr<Http::HttpClientConnection> conn, int connErrorCode) {
                        if ((conn == nullptr || !conn->IsOpen()) && connErrorCode == AWS_ERROR_SUCCESS)
                        {
                            connErrorCode = AWS_ERROR_UNKNOWN;
                        }

                        if (callback != nullptr)
                        {
                            callback(conn, connErrorCode);
                        }

                        if (connErrorCode == AWS_ERROR_SUCCESS)
                        {
                            MakeSignedRequest_SendRequest(conn, requestOptions, signedRequest);
                        }
                    });
            }
        });
}

void S3ObjectTransport::MakeSignedRequest_SendRequest(
    const std::shared_ptr<Http::HttpClientConnection> &conn,
    const Http::HttpRequestOptions &requestOptions,
    const std::shared_ptr<Http::HttpRequest> &signedRequest)
{
    AWS_FATAL_ASSERT(conn->IsOpen());

    Http::HttpRequestOptions requestOptionsToSend = requestOptions;
    requestOptionsToSend.request = signedRequest.get();

    ++m_activeRequestsCount;

    // NOTE: The captures of the connection and signed request is a work around to keep these shared
    // pointers alive until the stream is finished.
    requestOptionsToSend.onStreamComplete =
        [this, conn, signedRequest, requestOptions](Http::HttpStream &stream, int errorCode) {
            --m_activeRequestsCount;

            if (requestOptions.onStreamComplete != nullptr)
            {
                requestOptions.onStreamComplete(stream, errorCode);
            }
        };

    std::shared_ptr<Http::HttpClientStream> clientStream = conn->NewClientStream(requestOptionsToSend);

    if (clientStream == nullptr)
    {
        AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "Unable to open stream for S3ObjectTransport operation.");
    }
    else
    {
        clientStream->Activate();
    }
}

void S3ObjectTransport::AddContentLengthHeader(
    const std::shared_ptr<Http::HttpRequest> &request,
    const std::shared_ptr<Aws::Crt::Io::InputStream> &body)
{
    Http::HttpHeader contentLength;
    contentLength.name = ByteCursorFromCString("content-length");

    StringStream intValue;
    intValue << body->GetLength();
    String contentLengthVal = intValue.str();
    contentLength.value = ByteCursorFromCString(contentLengthVal.c_str());
    request->AddHeader(contentLength);
}

void S3ObjectTransport::PutObject(
    const std::shared_ptr<TransferState> &transferState,
    const Aws::Crt::String &key,
    const std::shared_ptr<Io::InputStream> &body,
    uint32_t flags,
    const TransferConnectionAcquired &connectionCallback,
    const PutObjectFinished &finishedCallback)
{
    AWS_FATAL_ASSERT(body.get() != nullptr);

    auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);

    AddContentLengthHeader(request, body);

    request->AddHeader(m_hostHeader);
    request->AddHeader(m_contentTypeHeader);
    request->SetBody(body);
    request->SetMethod(aws_http_method_put);

    StringStream keyPathStream;
    keyPathStream << "/" << key;

    String keyPath = keyPathStream.str();
    ByteCursor path = ByteCursorFromCString(keyPath.c_str());
    request->SetPath(path);

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "PutObject initiated for path %s...", keyPath.c_str());

    std::shared_ptr<String> etag = nullptr;

    if ((flags & (uint32_t)EPutObjectFlags::RetrieveETag) != 0)
    {
        etag = MakeShared<String>(g_allocator);
    }

    Http::HttpRequestOptions requestOptions;
    requestOptions.request = nullptr;
    requestOptions.onIncomingHeaders = [etag, transferState](
                                           Http::HttpStream &stream,
                                           enum aws_http_header_block headerBlock,
                                           const Http::HttpHeader *headersArray,
                                           std::size_t headersCount) {
        (void)stream;
        (void)headerBlock;

        if (transferState != nullptr)
        {
            transferState->ProcessHeaders(headersArray, headersCount);
        }

        if (etag == nullptr)
        {
            return;
        }

        for (size_t i = 0; i < headersCount; ++i)
        {
            const aws_byte_cursor &name = headersArray[i].name;

            if (aws_byte_cursor_eq_c_str(&name, "ETag"))
            {
                const aws_byte_cursor &value = headersArray[i].value;
                *etag = String((const char *)value.ptr, value.len);
            }
        }
    };

    requestOptions.onStreamComplete =
        [transferState, keyPath, etag, finishedCallback](Http::HttpStream &stream, int errorCode) {
            if (errorCode == AWS_ERROR_SUCCESS)
            {
                if (stream.GetResponseStatusCode() != 200)
                {
                    errorCode = AWS_ERROR_UNKNOWN;
                }

                aws_log_level logLevel = (errorCode != AWS_ERROR_SUCCESS) ? AWS_LL_ERROR : AWS_LL_INFO;

                AWS_LOGF(
                    logLevel,
                    AWS_LS_CRT_CPP_CANARY,
                    "PutObject finished for path %s with response status %d",
                    keyPath.c_str(),
                    stream.GetResponseStatusCode());
            }
            else
            {
                AWS_LOGF_DEBUG(
                    AWS_LS_CRT_CPP_CANARY,
                    "PutObject finished for path %s with error '%s'",
                    keyPath.c_str(),
                    aws_error_debug_str(errorCode));
            }

            if (transferState != nullptr)
            {
                transferState->SetTransferSuccess(errorCode == AWS_ERROR_SUCCESS);
            }

            finishedCallback(errorCode, etag);
        };

    std::shared_ptr<Http::HttpClientConnection> existingConn =
        transferState != nullptr ? transferState->GetConnection() : nullptr;

    MakeSignedRequest(
        existingConn,
        request,
        requestOptions,
        [keyPath, transferState, connectionCallback, finishedCallback](
            std::shared_ptr<Http::HttpClientConnection> conn, int32_t errorCode) {
            if (connectionCallback)
            {
                connectionCallback(conn, errorCode);
            }

            if (errorCode != AWS_ERROR_SUCCESS)
            {
                AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "Making signed request failed with error code %d", errorCode);
                finishedCallback(errorCode, nullptr);
            }
            else
            {
                // AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Setting transfer state address to %s", connAddr.c_str());

                if (transferState != nullptr)
                {
                    transferState->InitDataUpMetric();
                }

                if (transferState != nullptr)
                {
                    transferState->SetConnection(conn);
                }
            }
        });
}

void S3ObjectTransport::GetObject(
    const std::shared_ptr<TransferState> &transferState,
    const Aws::Crt::String &key,
    uint32_t partNumber,
    Aws::Crt::Http::OnIncomingBody onIncomingBody,
    const TransferConnectionAcquired &connectionCallback,
    const GetObjectFinished &getObjectFinished)
{
    auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
    request->AddHeader(m_hostHeader);

    request->SetMethod(aws_http_method_get);

    StringStream keyPathStream;
    keyPathStream << "/" << key;

    if (partNumber > 0)
    {
        keyPathStream << "?partNumber=" << partNumber;
    }

    String keyPath = keyPathStream.str();
    ByteCursor path = ByteCursorFromCString(keyPath.c_str());
    request->SetPath(path);

    Http::HttpRequestOptions requestOptions;
    requestOptions.request = nullptr;
    requestOptions.onIncomingBody = [transferState, onIncomingBody](Http::HttpStream &stream, const ByteCursor &cur) {
        if (transferState != nullptr)
        {
            transferState->AddDataDownMetric(cur.len);
        }

        if (onIncomingBody != nullptr)
        {
            onIncomingBody(stream, cur);
        }
    };

    requestOptions.onIncomingHeaders = [transferState](
                                           Http::HttpStream &stream,
                                           enum aws_http_header_block headerBlock,
                                           const Http::HttpHeader *headersArray,
                                           std::size_t headersCount) {
        if (transferState != nullptr)
        {
            transferState->ProcessHeaders(headersArray, headersCount);
        }
    };
    requestOptions.onStreamComplete =
        [keyPath, partNumber, transferState, getObjectFinished](Http::HttpStream &stream, int error) {
            int errorCode = error;

            if (errorCode == AWS_ERROR_SUCCESS)
            {
                int32_t successStatus = partNumber > 0 ? S3GetObjectResponseStatus_PartialContent : 200;

                if (stream.GetResponseStatusCode() != successStatus)
                {
                    errorCode = AWS_ERROR_UNKNOWN;
                }

                aws_log_level logLevel = (errorCode != AWS_ERROR_SUCCESS) ? AWS_LL_ERROR : AWS_LL_DEBUG;

                AWS_LOGF(
                    logLevel,
                    AWS_LS_CRT_CPP_CANARY,
                    "GetObject finished for path %s with response status %d",
                    keyPath.c_str(),
                    stream.GetResponseStatusCode());
            }
            else
            {
                AWS_LOGF_ERROR(
                    AWS_LS_CRT_CPP_CANARY,
                    "GetObject finished for path %s with error '%s'",
                    keyPath.c_str(),
                    aws_error_debug_str(errorCode));
            }

            if (transferState != nullptr)
            {
                transferState->SetTransferSuccess(errorCode == AWS_ERROR_SUCCESS);
            }

            getObjectFinished(errorCode);
        };

    std::shared_ptr<Http::HttpClientConnection> existingConn =
        transferState != nullptr ? transferState->GetConnection() : nullptr;

    MakeSignedRequest(
        existingConn,
        request,
        requestOptions,
        [transferState, connectionCallback, getObjectFinished](
            std::shared_ptr<Http::HttpClientConnection> conn, int32_t errorCode) {
            if (connectionCallback)
            {
                connectionCallback(conn, errorCode);
            }

            if (errorCode != AWS_ERROR_SUCCESS)
            {
                getObjectFinished(errorCode);
            }
            else
            {
                // AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Setting transfer state address to %s", connAddr.c_str());
                if (transferState != nullptr)
                {
                    transferState->InitDataDownMetric();
                }

                if (transferState != nullptr)
                {
                    transferState->SetConnection(conn);
                }
            }
        });
}

std::shared_ptr<MultipartUploadState> S3ObjectTransport::PutObjectMultipart(
    const Aws::Crt::String &key,
    std::uint64_t objectSize,
    std::uint32_t numParts,
    const GetPartStream &getPartStream,
    const PutObjectMultipartFinished &finishedCallback)
{
    std::shared_ptr<MultipartUploadState> multipartState =
        MakeShared<MultipartUploadState>(g_allocator, key, objectSize, numParts);

    CreateMultipartUpload(
        multipartState->GetKey(),
        [this, multipartState, getPartStream, finishedCallback](int errorCode, const Aws::Crt::String &uploadId) {
            if (errorCode != AWS_ERROR_SUCCESS)
            {
                multipartState->SetFinished(errorCode);
                return;
            }

            multipartState->SetUploadId(uploadId);

            UploadNextPart(multipartState, getPartStream, finishedCallback);
        });

    return multipartState;
}

void S3ObjectTransport::UploadNextPart(
    const std::shared_ptr<MultipartUploadState> &multipartState,
    const GetPartStream &getPartStream,
    const PutObjectMultipartFinished &finishedCallback)
{
    std::shared_ptr<TransferState> partTransferState = multipartState->PopNextPart();

    partTransferState->SetConnection(multipartState->GetConnection());

    StringStream keyPathStream;
    keyPathStream << multipartState->GetKey() << "?partNumber=" << partTransferState->GetPartNumber()
                  << "&uploadId=" << multipartState->GetUploadId();

    String keyPathStr = keyPathStream.str();

    PutObject(
        partTransferState,
        keyPathStr,
        getPartStream(partTransferState),
        (uint32_t)EPutObjectFlags::RetrieveETag,
        [multipartState](std::shared_ptr<Http::HttpClientConnection> connection, int32_t errorCode) {
            multipartState->SetConnection((errorCode == AWS_ERROR_SUCCESS) ? connection : nullptr);
        },
        [this, multipartState, partTransferState, getPartStream, finishedCallback](
            int32_t errorCode, std::shared_ptr<Aws::Crt::String> etag) {
            if (etag == nullptr)
            {
                errorCode = AWS_ERROR_UNKNOWN;
            }

            std::shared_ptr<Http::HttpClientConnection> conn = multipartState->GetConnection();

            if (conn != nullptr && !conn->IsOpen())
            {
                multipartState->SetConnection(nullptr);
            }

            if (errorCode == AWS_ERROR_SUCCESS)
            {
                multipartState->SetETag(partTransferState->GetPartIndex(), *etag);

                if (multipartState->IncNumPartsCompleted())
                {
                    multipartState->SetConnection(nullptr);

                    Aws::Crt::Vector<Aws::Crt::String> etags;
                    multipartState->GetETags(etags);

                    CompleteMultipartUpload(
                        multipartState->GetKey(),
                        multipartState->GetUploadId(),
                        etags,
                        [multipartState, finishedCallback](int32_t errorCode) {
                            multipartState->SetFinished(errorCode);

                            finishedCallback(errorCode, multipartState->GetNumParts());
                        });
                }
                else
                {
                    UploadNextPart(multipartState, getPartStream, finishedCallback);
                }

                AWS_LOGF_INFO(
                    AWS_LS_CRT_CPP_CANARY,
                    "UploadPart for path %s and part #%d (%d/%d) just returned code %d",
                    multipartState->GetKey().c_str(),
                    partTransferState->GetPartNumber(),
                    multipartState->GetNumPartsCompleted(),
                    multipartState->GetNumParts(),
                    errorCode);
            }
            else
            {
                AWS_LOGF_ERROR(
                    AWS_LS_CRT_CPP_CANARY,
                    "Upload part #%d failed with error code %d (\"%s\")",
                    partTransferState->GetPartNumber(),
                    errorCode,
                    aws_error_debug_str(errorCode));

                multipartState->RequeuePart(partTransferState);

                UploadNextPart(multipartState, getPartStream, finishedCallback);
            }
        });
}

std::shared_ptr<MultipartDownloadState> S3ObjectTransport::GetObjectMultipart(
    const Aws::Crt::String &key,
    std::uint32_t numParts,
    const ReceivePartCallback &receivePart,
    const GetObjectMultipartFinished &finishedCallback)
{
    std::shared_ptr<MultipartDownloadState> downloadState =
        MakeShared<MultipartDownloadState>(g_allocator, key, 0L, numParts);

    GetNextPart(downloadState, receivePart, finishedCallback);

    return downloadState;
}

void S3ObjectTransport::GetNextPart(
    const std::shared_ptr<MultipartDownloadState> &multipartState,
    const ReceivePartCallback &receiveObjectPartData,
    const GetObjectMultipartFinished &finishedCallback)
{
    std::shared_ptr<TransferState> partTransferState = multipartState->PopNextPart();

    partTransferState->SetConnection(multipartState->GetConnection());

    GetObject(
        partTransferState,
        multipartState->GetKey(),
        partTransferState->GetPartNumber(),
        [multipartState, partTransferState, receiveObjectPartData](Http::HttpStream &stream, const ByteCursor &data) {
            (void)stream;
            receiveObjectPartData(partTransferState, data);
        },
        [multipartState](std::shared_ptr<Http::HttpClientConnection> connection, int32_t errorCode) {
            multipartState->SetConnection((errorCode == AWS_ERROR_SUCCESS) ? connection : nullptr);
        },
        [this, multipartState, partTransferState, receiveObjectPartData, finishedCallback](int32_t errorCode) {
            const String &key = multipartState->GetKey();

            std::shared_ptr<Http::HttpClientConnection> conn = multipartState->GetConnection();

            if (conn != nullptr && !conn->IsOpen())
            {
                multipartState->SetConnection(nullptr);
            }

            if (errorCode != AWS_ERROR_SUCCESS)
            {
                AWS_LOGF_ERROR(
                    AWS_LS_CRT_CPP_CANARY,
                    "Did not receive part #%d for %s",
                    partTransferState->GetPartNumber(),
                    key.c_str());

                multipartState->RequeuePart(partTransferState);

                GetNextPart(multipartState, receiveObjectPartData, finishedCallback);
            }
            else
            {
                AWS_LOGF_INFO(
                    AWS_LS_CRT_CPP_CANARY, "Received part #%d for %s", partTransferState->GetPartNumber(), key.c_str());

                if (multipartState->IncNumPartsCompleted())
                {
                    AWS_LOGF_DEBUG(AWS_LS_CRT_CPP_CANARY, "Finished trying to get all parts for %s", key.c_str());
                    multipartState->SetConnection(nullptr);
                    multipartState->SetFinished();
                    finishedCallback(errorCode);
                }
                else
                {
                    GetNextPart(multipartState, receiveObjectPartData, finishedCallback);
                }
            }
        });
}

void S3ObjectTransport::CreateMultipartUpload(
    const Aws::Crt::String &key,
    const CreateMultipartUploadFinished &finishedCallback)
{
    auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
    request->AddHeader(m_hostHeader);
    request->AddHeader(m_contentTypeHeader);
    request->SetMethod(aws_http_method_post);

    String keyPath = "/" + key + "?uploads";
    ByteCursor path = ByteCursorFromCString(keyPath.c_str());
    request->SetPath(path);

    std::shared_ptr<String> uploadId = MakeShared<String>(g_allocator);

    Http::HttpRequestOptions requestOptions;
    requestOptions.request = nullptr;
    requestOptions.onIncomingBody = [uploadId, keyPath](Http::HttpStream &stream, const ByteCursor &data) {
        (void)stream;

        const String uploadIdOpenTag("<UploadId>");
        const String uploadIdCloseTag("</UploadId>");

        String dataString((const char *)data.ptr);
        size_t uploadIdOpenIndex = dataString.find(uploadIdOpenTag);

        if (uploadIdOpenIndex == String::npos)
        {
            AWS_LOGF_ERROR(
                AWS_LS_CRT_CPP_CANARY, "CreateMultipartUpload response does not have an UploadId opening tag.");
            return;
        }

        size_t uploadIdCloseIndex = dataString.find(uploadIdCloseTag, uploadIdOpenIndex);

        if (uploadIdCloseIndex == String::npos)
        {
            AWS_LOGF_ERROR(
                AWS_LS_CRT_CPP_CANARY, "CreateMultipartUpload response does not have an UploadId closing tag.");
            return;
        }

        size_t uploadIdStart = uploadIdOpenIndex + uploadIdOpenTag.length();
        size_t uploadIdLength = uploadIdCloseIndex - uploadIdStart;

        *uploadId = dataString.substr(uploadIdStart, uploadIdLength);
    };
    requestOptions.onStreamComplete = [uploadId, keyPath, finishedCallback](Http::HttpStream &stream, int errorCode) {
        if (errorCode == AWS_ERROR_SUCCESS && uploadId->empty())
        {
            errorCode = AWS_ERROR_UNKNOWN;
        }

        if (errorCode == AWS_ERROR_SUCCESS)
        {
            if (stream.GetResponseStatusCode() != 200)
            {
                errorCode = AWS_ERROR_UNKNOWN;
            }

            aws_log_level logLevel = (errorCode != AWS_ERROR_SUCCESS) ? AWS_LL_ERROR : AWS_LL_DEBUG;

            AWS_LOGF(
                logLevel,
                AWS_LS_CRT_CPP_CANARY,
                "Created multipart upload for path %s with response status %d.",
                keyPath.c_str(),
                stream.GetResponseStatusCode());
        }
        else
        {
            AWS_LOGF_ERROR(
                AWS_LS_CRT_CPP_CANARY,
                "Created multipart upload for path %s failed with error '%s'",
                keyPath.c_str(),
                aws_error_debug_str(errorCode));
        }

        finishedCallback(errorCode, *uploadId);
    };

    AWS_LOGF_DEBUG(AWS_LS_CRT_CPP_CANARY, "Creating multipart upload for %s...", keyPath.c_str());

    MakeSignedRequest(
        nullptr,
        request,
        requestOptions,
        [finishedCallback](std::shared_ptr<Http::HttpClientConnection> conn, int32_t errorCode) {
            if (errorCode != AWS_ERROR_SUCCESS)
            {
                finishedCallback(errorCode, "");
            }
        });
}

void S3ObjectTransport::CompleteMultipartUpload(
    const Aws::Crt::String &key,
    const Aws::Crt::String &uploadId,
    const Aws::Crt::Vector<Aws::Crt::String> &etags,
    const CompleteMultipartUploadFinished &finishedCallback)
{
    AWS_LOGF_DEBUG(AWS_LS_CRT_CPP_CANARY, "Completing multipart upload for %s...", key.c_str());

    auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
    request->AddHeader(m_hostHeader);
    request->SetMethod(aws_http_method_post);

    std::shared_ptr<StringStream> xmlContents = MakeShared<StringStream>(g_allocator);
    *xmlContents << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n";
    *xmlContents << "<CompleteMultipartUpload xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">\n";

    for (size_t i = 0; i < etags.size(); ++i)
    {
        const Aws::Crt::String &etag = etags[i];
        size_t partNumber = i + 1;

        *xmlContents << "   <Part>\n";
        *xmlContents << "       <ETag>" << etag << "</ETag>\n";
        *xmlContents << "       <PartNumber>" << partNumber << "</PartNumber>\n";
        *xmlContents << "   </Part>\n";
    }

    *xmlContents << "</CompleteMultipartUpload>";

    std::shared_ptr<Io::StdIOStreamInputStream> body =
        MakeShared<Io::StdIOStreamInputStream>(g_allocator, xmlContents, g_allocator);
    request->SetBody(body);

    AddContentLengthHeader(request, body);

    StringStream keyPathStream;
    keyPathStream << "/" << key << "?uploadId=" << uploadId;
    String keyPath = keyPathStream.str();
    ByteCursor path = ByteCursorFromCString(keyPath.c_str());
    request->SetPath(path);

    Http::HttpRequestOptions requestOptions;
    requestOptions.request = nullptr;
    requestOptions.onStreamComplete = [keyPath, finishedCallback](Http::HttpStream &stream, int errorCode) {
        if (errorCode == AWS_ERROR_SUCCESS)
        {
            if (stream.GetResponseStatusCode() != 200)
            {
                errorCode = AWS_ERROR_UNKNOWN;
            }

            aws_log_level logLevel = (errorCode != AWS_ERROR_SUCCESS) ? AWS_LL_ERROR : AWS_LL_DEBUG;

            AWS_LOGF(
                logLevel,
                AWS_LS_CRT_CPP_CANARY,
                "Finished multipart upload for path %s with response status %d.",
                keyPath.c_str(),
                stream.GetResponseStatusCode());
        }
        else
        {
            AWS_LOGF_ERROR(
                AWS_LS_CRT_CPP_CANARY,
                "Finished multipart upload for path %s with error '%s'",
                keyPath.c_str(),
                aws_error_debug_str(errorCode));
        }

        finishedCallback(errorCode);
    };

    MakeSignedRequest(
        nullptr,
        request,
        requestOptions,
        [finishedCallback](std::shared_ptr<Http::HttpClientConnection> conn, int32_t errorCode) {
            if (errorCode != AWS_ERROR_SUCCESS)
            {
                finishedCallback(errorCode);
            }
        });
}

void S3ObjectTransport::AbortMultipartUpload(
    const Aws::Crt::String &key,
    const Aws::Crt::String &uploadId,
    const AbortMultipartUploadFinished &finishedCallback)
{
    AWS_LOGF_DEBUG(AWS_LS_CRT_CPP_CANARY, "Aborting multipart upload for %s...", key.c_str());

    auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
    request->AddHeader(m_hostHeader);
    request->SetMethod(aws_http_method_delete);

    String keyPath = "/" + key + "?uploadId=" + uploadId;
    ByteCursor keyPathByteCursor = ByteCursorFromCString(keyPath.c_str());
    request->SetPath(keyPathByteCursor);

    Http::HttpRequestOptions requestOptions;
    requestOptions.request = nullptr;
    requestOptions.onStreamComplete = [uploadId, keyPath, finishedCallback](Http::HttpStream &stream, int errorCode) {
        if (errorCode == AWS_ERROR_SUCCESS)
        {
            if (stream.GetResponseStatusCode() != 204)
            {
                errorCode = AWS_ERROR_UNKNOWN;
            }

            aws_log_level logLevel = (errorCode != AWS_ERROR_SUCCESS) ? AWS_LL_ERROR : AWS_LL_DEBUG;

            AWS_LOGF(
                logLevel,
                AWS_LS_CRT_CPP_CANARY,
                "Abort multipart upload for path %s finished with response status %d.",
                keyPath.c_str(),
                stream.GetResponseStatusCode());
        }
        else
        {
            AWS_LOGF_ERROR(
                AWS_LS_CRT_CPP_CANARY,
                "Abort multipart upload for path %s failed with error '%s'",
                keyPath.c_str(),
                aws_error_debug_str(errorCode));
        }

        finishedCallback(errorCode);
    };

    MakeSignedRequest(
        nullptr,
        request,
        requestOptions,
        [finishedCallback](std::shared_ptr<Http::HttpClientConnection> conn, int32_t errorCode) {
            if (errorCode != AWS_ERROR_SUCCESS)
            {
                finishedCallback(errorCode);
            }
        });
}
