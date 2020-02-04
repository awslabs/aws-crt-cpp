/*
 * Copyright 2010-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
#include <aws/crt/external/tinyxml2.h>
#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/Stream.h>
#include <aws/io/stream.h>
#include <inttypes.h>
#include <iostream>

#if defined(_WIN32)
#    undef min
#endif

using namespace Aws::Crt;

const uint64_t S3ObjectTransport::MaxPartSizeBytes = 500ULL * 1000ULL * 1000ULL;
const uint32_t S3ObjectTransport::MaxStreams =
    (100ULL * 1000ULL * 1000ULL * 1000ULL) / S3ObjectTransport::MaxPartSizeBytes;
const int32_t S3ObjectTransport::S3GetObjectResponseStatus_PartialContent = 206;
const bool S3ObjectTransport::SingleConnectionPerMultipartUpload = false;

S3ObjectTransport::S3ObjectTransport(CanaryApp &canaryApp, const Aws::Crt::String &bucket)
    : m_canaryApp(canaryApp), m_bucketName(bucket),
      m_uploadProcessor(canaryApp.eventLoopGroup, S3ObjectTransport::MaxStreams),
      m_downloadProcessor(canaryApp.eventLoopGroup, S3ObjectTransport::MaxStreams)
{
    Http::HttpClientConnectionManagerOptions connectionManagerOptions;
    m_endpoint = m_bucketName + ".s3." + canaryApp.region + ".amazonaws.com";
    connectionManagerOptions.ConnectionOptions.HostName = m_endpoint;
    connectionManagerOptions.ConnectionOptions.Port = 443;
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetConnectTimeoutMs(3000);
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetSocketType(AWS_SOCKET_STREAM);
    connectionManagerOptions.ConnectionOptions.InitialWindowSize = SIZE_MAX;

    aws_byte_cursor serverName = ByteCursorFromCString(m_endpoint.c_str());

    auto connOptions = canaryApp.tlsContext.NewConnectionOptions();
    connOptions.SetServerName(serverName);
    connectionManagerOptions.ConnectionOptions.TlsOptions = connOptions;
    connectionManagerOptions.ConnectionOptions.Bootstrap = &canaryApp.bootstrap;
    connectionManagerOptions.MaxConnections = 1000;

    m_connManager =
        Http::HttpClientConnectionManager::NewClientConnectionManager(connectionManagerOptions, g_allocator);

    m_hostHeader.name = ByteCursorFromCString("host");
    m_hostHeader.value = ByteCursorFromCString(m_endpoint.c_str());

    m_contentTypeHeader.name = ByteCursorFromCString("content-type");
    m_contentTypeHeader.value = ByteCursorFromCString("text/plain");
}

void S3ObjectTransport::MakeSignedRequest(
    const std::shared_ptr<Http::HttpClientConnection> &existingConn,
    const std::shared_ptr<Http::HttpRequest> &request,
    const Http::HttpRequestOptions &requestOptions,
    ErrorCallback errorCallback)
{
    Auth::AwsSigningConfig signingConfig(g_allocator);
    signingConfig.SetRegion(m_canaryApp.region);
    signingConfig.SetCredentialsProvider(m_canaryApp.credsProvider);
    signingConfig.SetService("s3");
    signingConfig.SetBodySigningType(Auth::BodySigningType::UnsignedPayload);
    signingConfig.SetSigningTimepoint(DateTime::Now());
    signingConfig.SetSigningAlgorithm(Auth::SigningAlgorithm::SigV4Header);

    m_canaryApp.signer->SignRequest(
        request,
        signingConfig,
        [this, existingConn, requestOptions, errorCallback](
            const std::shared_ptr<Aws::Crt::Http::HttpRequest> &signedRequest, int signingErrorCode) {
            if (signingErrorCode != AWS_ERROR_SUCCESS)
            {
                if (errorCallback != nullptr)
                {
                    errorCallback(signingErrorCode);
                }
                return;
            }

            if (existingConn != nullptr)
            {
                MakeSignedRequest_SendRequest(existingConn, requestOptions, signedRequest);
            }
            else
            {
                m_connManager->AcquireConnection(
                    [this, requestOptions, signedRequest, errorCallback](
                        std::shared_ptr<Http::HttpClientConnection> conn, int connErrorCode) {
                        if (connErrorCode == AWS_ERROR_SUCCESS)
                        {
                            MakeSignedRequest_SendRequest(conn, requestOptions, signedRequest);
                        }
                        else if (errorCallback != nullptr)
                        {
                            errorCallback(connErrorCode);
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
    Http::HttpRequestOptions requestOptionsToSend = requestOptions;
    requestOptionsToSend.request = signedRequest.get();

    // NOTE: The captures of the connection and signed request is a work around to keep those shared
    // pointers alive until the stream is finished.  Tasks can be scheduled that rely on these things
    // being alive which can cause crashes when they aren't around.
    requestOptionsToSend.onStreamComplete =
        [conn, requestOptions, signedRequest](Http::HttpStream &stream, int errorCode) {
            if (requestOptions.onStreamComplete != nullptr)
            {
                requestOptions.onStreamComplete(stream, errorCode);
            }
        };

    conn->NewClientStream(requestOptionsToSend);
}

void S3ObjectTransport::AddContentLengthHeader(
    std::shared_ptr<Http::HttpRequest> request,
    aws_input_stream *inputStream)
{
    Http::HttpHeader contentLength;
    contentLength.name = ByteCursorFromCString("content-length");

    int64_t streamLen = 0;

    aws_input_stream_get_length(inputStream, &streamLen);
    StringStream intValue;
    intValue << streamLen;
    String contentLengthVal = intValue.str();
    contentLength.value = ByteCursorFromCString(contentLengthVal.c_str());
    request->AddHeader(contentLength);
}

void S3ObjectTransport::PutObject(
    const Aws::Crt::String &key,
    aws_input_stream *inputStream,
    const PutObjectFinished &finishedCallback)
{
    AWS_FATAL_ASSERT(inputStream != nullptr);

    m_connManager->AcquireConnection([this, key, inputStream, finishedCallback](
                                         std::shared_ptr<Http::HttpClientConnection> conn, int connErrorCode) {
        if (connErrorCode != AWS_ERROR_SUCCESS)
        {
            aws_input_stream_destroy(inputStream);
            finishedCallback(connErrorCode, nullptr);
            return;
        }

        PutObject(conn, key, inputStream, 0, finishedCallback);
    });
}

void S3ObjectTransport::PutObject(
    const std::shared_ptr<Http::HttpClientConnection> &conn,
    const Aws::Crt::String &key,
    aws_input_stream *inputStream,
    uint32_t flags,
    const PutObjectFinished &finishedCallback)
{
    AWS_FATAL_ASSERT(inputStream != nullptr);

    auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);

    AddContentLengthHeader(request, inputStream);

    request->AddHeader(m_hostHeader);
    request->AddHeader(m_contentTypeHeader);

    // By passing the stream here, the request is accepting ownership of the memory for inputStream.
    aws_http_message_set_body_stream(request->GetUnderlyingMessage(), inputStream);
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
    AWS_ZERO_STRUCT(requestOptions);
    requestOptions.onIncomingHeaders = [etag](
                                           Http::HttpStream &stream,
                                           enum aws_http_header_block headerBlock,
                                           const Http::HttpHeader *headersArray,
                                           std::size_t headersCount) {
        (void)stream;
        (void)headerBlock;

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
    requestOptions.onStreamComplete = [keyPath, etag, finishedCallback](Http::HttpStream &stream, int errorCode) {
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
            AWS_LOGF_ERROR(
                AWS_LS_CRT_CPP_CANARY,
                "PutObject finished for path %s with error '%s'",
                keyPath.c_str(),
                aws_error_debug_str(errorCode));
        }

        finishedCallback(errorCode, etag);
    };

    MakeSignedRequest(
        conn, request, requestOptions, [finishedCallback](int32_t errorCode) { finishedCallback(errorCode, nullptr); });
}

void S3ObjectTransport::PutObjectMultipart(
    const Aws::Crt::String &key,
    std::uint64_t objectSize,
    SendPartCallback sendPart,
    const PutObjectMultipartFinished &finishedCallback)
{
    uint32_t numParts = GetNumParts(objectSize);

    std::shared_ptr<MultipartUploadState> uploadState =
        MakeShared<MultipartUploadState>(g_allocator, key, objectSize, numParts);

    // Set the callback that the MultipartTransferProcessor will use to process the part.
    // In this case, it will try to upload it.
    uploadState->SetProcessPartCallback(
        [this, uploadState, sendPart, finishedCallback](
            MultipartTransferState::PartInfo &partInfo, MultipartTransferState::PartFinishedCallback partFinished) {
            aws_input_stream *partInputStream = sendPart(partInfo);

            UploadPart(uploadState, partInfo, partInputStream, partFinished);
        });

    // Set the callback that will be called when something flags the upload state as finished.  Finished
    // can happen due to success or failure.
    uploadState->SetFinishedCallback([this, uploadState, key, numParts, finishedCallback](int32_t errorCode) {
        if (errorCode != AWS_ERROR_SUCCESS)
        {
            AbortMultipartUpload(
                uploadState->GetConnection(),
                key,
                uploadState->GetUploadId(),
                [numParts, errorCode, finishedCallback](int32_t) { finishedCallback(errorCode, numParts); });
        }
        else
        {
            finishedCallback(errorCode, numParts);
        }
    });

    m_connManager->AcquireConnection(
        [this, uploadState](std::shared_ptr<Http::HttpClientConnection> conn, int connErrorCode) {
            if (connErrorCode != AWS_ERROR_SUCCESS)
            {
                uploadState->SetFinished(connErrorCode);
                return;
            }

            // If SingleConnectinPerMultipartUpload is set to true, we cache the connection
            // on the uploadState so that it can be re-used.  Otherwise, a new connection
            // will be made for each request made for the multipart upload.
            if (SingleConnectionPerMultipartUpload)
            {
                uploadState->SetConnection(conn);
            }

            // Go ahead and start the multipart upload, pushing it into the queue
            // if we were able to successfully issue the CreateMultipartUpload request.
            CreateMultipartUpload(
                conn, uploadState->GetKey(), [this, uploadState](int errorCode, const Aws::Crt::String &uploadId) {
                    if (errorCode != AWS_ERROR_SUCCESS)
                    {
                        uploadState->SetFinished(errorCode);
                        return;
                    }

                    uploadState->SetUploadId(uploadId);
                    m_uploadProcessor.PushQueue(uploadState);
                });
        });
}

void S3ObjectTransport::UploadPart(
    const std::shared_ptr<MultipartUploadState> &state,
    MultipartTransferState::PartInfo &partInfo,
    aws_input_stream *partInputStream,
    const MultipartTransferState::PartFinishedCallback &partFinished)
{
    StringStream keyPathStream;
    keyPathStream << state->GetKey() << "?partNumber=" << partInfo.partNumber << "&uploadId=" << state->GetUploadId();

    partInfo.uploadStartTime = DateTime::Now();

    PutObject(
        state->GetConnection(),
        keyPathStream.str(),
        partInputStream,
        (uint32_t)EPutObjectFlags::RetrieveETag,
        [this, state, partInfo, partFinished](int32_t errorCode, std::shared_ptr<Aws::Crt::String> etag) {
            if (errorCode == AWS_ERROR_SUCCESS && etag == nullptr)
            {
                errorCode = AWS_ERROR_UNKNOWN;
            }

            if (errorCode == AWS_ERROR_SUCCESS)
            {
                state->SetETag(partInfo.partIndex, *etag);

                DateTime uploadEndTime = DateTime::Now();
                double interval = (double)(uploadEndTime.Millis() - partInfo.uploadStartTime.Millis());
                int sampleMax = 20;

                AWS_LOGF_INFO(
                    AWS_LS_CRT_CPP_CANARY,
                    "Sending %d BytesUp metric samples for %" PRId64 " bytes over %" PRId64 " milliseconds",
                    sampleMax,
                    partInfo.sizeInBytes,
                    (uint64_t)interval);

                for (int i = 1; i <= sampleMax; ++i)
                {
                    double alpha = (double)i / (double)sampleMax;
                    DateTime lerpedTime =
                        partInfo.uploadStartTime + std::chrono::milliseconds((uint64_t)(alpha * interval));
                    double bytesSentApprox = (double)partInfo.sizeInBytes / (double)sampleMax;

                    Metric uploadMetric;
                    uploadMetric.MetricName = "BytesUp";
                    uploadMetric.Timestamp = lerpedTime;
                    uploadMetric.Value = bytesSentApprox;
                    uploadMetric.Unit = MetricUnit::Bytes;

                    m_canaryApp.publisher->AddDataPoint(uploadMetric);
                }

                if (state->IncNumPartsCompleted())
                {
                    Aws::Crt::Vector<Aws::Crt::String> etags;
                    state->GetETags(etags);

                    CompleteMultipartUpload(
                        state->GetConnection(),
                        state->GetKey(),
                        state->GetUploadId(),
                        etags,
                        [state](int32_t errorCode) { state->SetFinished(errorCode); });
                }
            }
            else
            {
                state->SetFinished(errorCode);
            }

            AWS_LOGF_INFO(
                AWS_LS_CRT_CPP_CANARY,
                "UploadPart for path %s and part #%d (%d/%d) just returned code %d",
                state->GetKey().c_str(),
                partInfo.partNumber,
                state->GetNumPartsCompleted(),
                state->GetNumParts(),
                errorCode);

            partFinished();
        });
}

void S3ObjectTransport::GetObject(
    const Aws::Crt::String &key,
    Aws::Crt::Http::OnIncomingBody onIncomingBody,
    const GetObjectFinished &getObjectFinished)
{
    m_connManager->AcquireConnection([this, key, onIncomingBody, getObjectFinished](
                                         std::shared_ptr<Http::HttpClientConnection> conn, int connErrorCode) {
        if (connErrorCode != AWS_ERROR_SUCCESS)
        {
            getObjectFinished(connErrorCode);
            return;
        }

        GetObject(conn, key, 0, onIncomingBody, getObjectFinished);
    });
}

void S3ObjectTransport::GetObject(
    const std::shared_ptr<Http::HttpClientConnection> &conn,
    const Aws::Crt::String &key,
    uint32_t partNumber,
    Aws::Crt::Http::OnIncomingBody onIncomingBody,
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
    AWS_ZERO_STRUCT(requestOptions);
    requestOptions.onIncomingBody = onIncomingBody;
    requestOptions.onStreamComplete = [keyPath, partNumber, getObjectFinished](Http::HttpStream &stream, int error) {
        int errorCode = error;

        if (errorCode == AWS_ERROR_SUCCESS)
        {
            int32_t successStatus = partNumber > 0 ? S3GetObjectResponseStatus_PartialContent : 200;

            if (stream.GetResponseStatusCode() != successStatus)
            {
                errorCode = AWS_ERROR_UNKNOWN;
            }

            aws_log_level logLevel = (errorCode != AWS_ERROR_SUCCESS) ? AWS_LL_ERROR : AWS_LL_INFO;

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

        getObjectFinished(errorCode);
    };

    MakeSignedRequest(
        conn, request, requestOptions, [getObjectFinished](int32_t errorCode) { getObjectFinished(errorCode); });
}

void S3ObjectTransport::GetObjectMultipart(
    const Aws::Crt::String &key,
    std::uint32_t numParts,
    const ReceivePartCallback &receivePart,
    const GetObjectMultipartFinished &finishedCallback)
{
    std::shared_ptr<MultipartDownloadState> downloadState =
        MakeShared<MultipartDownloadState>(g_allocator, key, 0L, numParts);

    // Set the callback that the MultipartTransferProcessor will use to process the part.
    // In this case, try to download it.
    downloadState->SetProcessPartCallback([this, downloadState, receivePart](
                                              MultipartTransferState::PartInfo &partInfo,
                                              const MultipartTransferState::PartFinishedCallback &partFinished) {
        GetPart(downloadState, partInfo, receivePart, partFinished);
    });

    // Set the callback that will be called when something flags the upload state as finished.  Finished
    // can happen due to success or failure.
    downloadState->SetFinishedCallback([finishedCallback](int32_t errorCode) { finishedCallback(errorCode); });

    m_connManager->AcquireConnection(
        [this, downloadState](std::shared_ptr<Http::HttpClientConnection> conn, int connErrorCode) {
            if (connErrorCode != AWS_ERROR_SUCCESS)
            {
                downloadState->SetFinished(connErrorCode);
                return;
            }

            // If SingleConnectinPerMultipartUpload is set to true, we cache the connection
            // on the uploadState so that it can be re-used.  Otherwise, a new connection
            // will be made for each request made for the multipart upload.
            if (SingleConnectionPerMultipartUpload)
            {
                downloadState->SetConnection(conn);
            }

            m_downloadProcessor.PushQueue(downloadState);
        });
}

void S3ObjectTransport::GetPart(
    std::shared_ptr<MultipartDownloadState> downloadState,
    MultipartTransferState::PartInfo &partInfo,
    const ReceivePartCallback &receiveObjectPartData,
    const MultipartTransferState::PartFinishedCallback &partFinished)
{
    GetObject(
        downloadState->GetConnection(),
        downloadState->GetKey(),
        partInfo.partNumber,
        [downloadState, partInfo, receiveObjectPartData](Http::HttpStream &stream, const ByteCursor &data) {
            (void)stream;

            receiveObjectPartData(partInfo, data);
        },
        [downloadState, partInfo, partFinished](int32_t errorCode) {
            const String &key = downloadState->GetKey();

            if (errorCode != AWS_ERROR_SUCCESS)
            {
                AWS_LOGF_ERROR(
                    AWS_LS_CRT_CPP_CANARY, "Did not receive part #%d for %s", partInfo.partNumber, key.c_str());

                downloadState->SetFinished(errorCode);
            }
            else
            {
                AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Received part #%d for %s", partInfo.partNumber, key.c_str());

                if (downloadState->IncNumPartsCompleted())
                {
                    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "All parts received for %s", key.c_str());
                    downloadState->SetFinished();
                }
            }

            partFinished();
        });
}

uint32_t S3ObjectTransport::GetNumParts(uint64_t objectSize) const
{
    uint64_t numParts = objectSize / MaxPartSizeBytes;

    if ((objectSize % MaxPartSizeBytes) > 0)
    {
        ++numParts;
    }

    AWS_FATAL_ASSERT(numParts <= static_cast<uint64_t>(UINT32_MAX));

    return static_cast<uint32_t>(numParts);
}

void S3ObjectTransport::CreateMultipartUpload(
    const std::shared_ptr<Http::HttpClientConnection> &conn,
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
    AWS_ZERO_STRUCT(requestOptions);
    requestOptions.onIncomingBody = [uploadId, keyPath](Http::HttpStream &stream, const ByteCursor &data) {
        (void)stream;

        tinyxml2::XMLDocument xmlDocument;
        tinyxml2::XMLError parseError = xmlDocument.Parse((const char *)data.ptr, data.len);

        if (parseError != tinyxml2::XML_SUCCESS)
        {
            return;
        }

        tinyxml2::XMLElement *rootElement = xmlDocument.RootElement();

        if (rootElement == nullptr)
        {
            return;
        }

        tinyxml2::XMLElement *uploadIdElement = rootElement->FirstChildElement("UploadId");

        if (uploadIdElement == nullptr)
        {
            return;
        }
        else
        {
            *uploadId = uploadIdElement->GetText();
        }
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

            aws_log_level logLevel = (errorCode != AWS_ERROR_SUCCESS) ? AWS_LL_ERROR : AWS_LL_INFO;

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

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Creating multipart upload for %s...", keyPath.c_str());

    MakeSignedRequest(
        conn, request, requestOptions, [finishedCallback](int32_t errorCode) { finishedCallback(errorCode, ""); });
}

void S3ObjectTransport::CompleteMultipartUpload(
    const std::shared_ptr<Http::HttpClientConnection> &conn,
    const Aws::Crt::String &key,
    const Aws::Crt::String &uploadId,
    const Aws::Crt::Vector<Aws::Crt::String> &etags,
    const CompleteMultipartUploadFinished &finishedCallback)
{
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Completing multipart upload for %s...", key.c_str());

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

    // By passing the stream here, the request is accepting ownership of the memory for inputStream.
    aws_input_stream *inputStream = Aws::Crt::Io::AwsInputStreamNewCpp(xmlContents, aws_default_allocator());
    aws_http_message_set_body_stream(request->GetUnderlyingMessage(), inputStream);

    AddContentLengthHeader(request, inputStream);

    StringStream keyPathStream;
    keyPathStream << "/" << key << "?uploadId=" << uploadId;
    String keyPath = keyPathStream.str();
    ByteCursor path = ByteCursorFromCString(keyPath.c_str());
    request->SetPath(path);

    Http::HttpRequestOptions requestOptions;
    AWS_ZERO_STRUCT(requestOptions);
    requestOptions.onStreamComplete = [keyPath, finishedCallback](Http::HttpStream &stream, int errorCode) {
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
        conn, request, requestOptions, [finishedCallback](int32_t errorCode) { finishedCallback(errorCode); });
}

void S3ObjectTransport::AbortMultipartUpload(
    const std::shared_ptr<Http::HttpClientConnection> &conn,
    const Aws::Crt::String &key,
    const Aws::Crt::String &uploadId,
    const AbortMultipartUploadFinished &finishedCallback)
{
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Aborting multipart upload for %s...", key.c_str());

    auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
    request->AddHeader(m_hostHeader);
    request->SetMethod(aws_http_method_delete);

    String keyPath = "/" + key + "?uploadId=" + uploadId;
    ByteCursor keyPathByteCursor = ByteCursorFromCString(keyPath.c_str());
    request->SetPath(keyPathByteCursor);

    Http::HttpRequestOptions requestOptions;
    AWS_ZERO_STRUCT(requestOptions);
    requestOptions.onStreamComplete = [uploadId, keyPath, finishedCallback](Http::HttpStream &stream, int errorCode) {
        if (errorCode == AWS_ERROR_SUCCESS)
        {
            if (stream.GetResponseStatusCode() != 204)
            {
                errorCode = AWS_ERROR_UNKNOWN;
            }

            aws_log_level logLevel = (errorCode != AWS_ERROR_SUCCESS) ? AWS_LL_ERROR : AWS_LL_INFO;

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
        conn, request, requestOptions, [finishedCallback](int32_t errorCode) { finishedCallback(errorCode); });
}
