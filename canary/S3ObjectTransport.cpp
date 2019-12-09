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

#include <aws/cal/hash.h>
#include <aws/common/encoding.h>
#include <aws/crt/external/tinyxml2.h>
#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/stream.h>
#include <aws/io/stream.h>

#include <iostream>

using namespace Aws::Crt;

S3ObjectTransport::S3ObjectTransport(
    const Aws::Crt::String &region,
    const Aws::Crt::String &bucket,
    Aws::Crt::Io::TlsContext &tlsContext,
    Aws::Crt::Io::ClientBootstrap &clientBootstrap,
    const std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> &credsProvider,
    const std::shared_ptr<Aws::Crt::Auth::Sigv4HttpRequestSigner> &signer,
    size_t maxCons)
    : m_signer(signer), m_credsProvider(credsProvider), m_region(region), m_bucketName(bucket)
{
    Http::HttpClientConnectionManagerOptions connectionManagerOptions;
    m_endpoint = m_bucketName + ".s3." + m_region + ".amazonaws.com";
    connectionManagerOptions.ConnectionOptions.HostName = m_endpoint;
    connectionManagerOptions.ConnectionOptions.Port = 443;
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetConnectTimeoutMs(3000);
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetSocketType(AWS_SOCKET_STREAM);
    connectionManagerOptions.ConnectionOptions.InitialWindowSize = SIZE_MAX;

    aws_byte_cursor serverName = ByteCursorFromCString(m_endpoint.c_str());

    auto connOptions = tlsContext.NewConnectionOptions();
    connOptions.SetServerName(serverName);
    connectionManagerOptions.ConnectionOptions.TlsOptions = connOptions;
    connectionManagerOptions.ConnectionOptions.Bootstrap = &clientBootstrap;
    connectionManagerOptions.MaxConnections = maxCons;

    m_connManager =
        Http::HttpClientConnectionManager::NewClientConnectionManager(connectionManagerOptions, g_allocator);

    m_hostHeader.name = ByteCursorFromCString("host");
    m_hostHeader.value = ByteCursorFromCString(m_endpoint.c_str());

    m_contentTypeHeader.name = ByteCursorFromCString("content-type");
    m_contentTypeHeader.value = ByteCursorFromCString("text/plain");

    m_upStreamsAvailable = MaxStreams;

    aws_mutex_init(&m_upStreamsAvailableMutex);
    aws_mutex_init(&m_multipartUploadQueueMutex);
}

S3ObjectTransport::~S3ObjectTransport()
{
    aws_mutex_clean_up(&m_upStreamsAvailableMutex);
    aws_mutex_clean_up(&m_multipartUploadQueueMutex);
}

void S3ObjectTransport::PutObject(
    const Aws::Crt::String &key,
    aws_input_stream *inputStream,
    uint32_t flags,
    PutObjectCompleted completedCallback)
{
    auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);

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

    request->AddHeader(m_hostHeader);
    request->AddHeader(m_contentTypeHeader);

    aws_http_message_set_body_stream(request->GetUnderlyingMessage(), inputStream);
    request->SetMethod(aws_http_method_put);

    StringStream keyPathStream;
    keyPathStream << "/" << key;

    String keyPath = keyPathStream.str();
    ByteCursor path = ByteCursorFromCString(keyPath.c_str());
    request->SetPath(path);

    Auth::AwsSigningConfig signingConfig(g_allocator);
    signingConfig.SetRegion(m_region);
    signingConfig.SetCredentialsProvider(m_credsProvider);
    signingConfig.SetService("s3");
    signingConfig.SetBodySigningType(Auth::BodySigningType::UnsignedPayload);
    signingConfig.SetSigningTimepoint(DateTime::Now());
    signingConfig.SetSigningAlgorithm(Auth::SigningAlgorithm::SigV4Header);

    AWS_LOGF_INFO(AWS_LS_COMMON_GENERAL, "PutObject initiated for path %s...", keyPath.c_str());

    std::shared_ptr<Aws::Crt::String> etag = nullptr;

    if ((flags & (uint32_t)EPutObjectFlags::RetrieveETag) != 0)
    {
        etag = std::make_shared<Aws::Crt::String>();
    }

    m_signer->SignRequest(
        request,
        signingConfig,
        [this, keyPath, inputStream, etag, completedCallback](
            const std::shared_ptr<Aws::Crt::Http::HttpRequest> &signedRequest, int signingError) {
            if (signingError != AWS_OP_SUCCESS)
            {
                completedCallback(signingError, nullptr);
                return;
            }

            m_connManager->AcquireConnection([signedRequest, keyPath, inputStream, etag, completedCallback](
                                                 std::shared_ptr<Http::HttpClientConnection> conn, int connError) {
                if (connError != AWS_OP_SUCCESS)
                {
                    completedCallback(connError, nullptr);
                    return;
                }

                Http::HttpRequestOptions requestOptions;
                AWS_ZERO_STRUCT(requestOptions);
                requestOptions.request = signedRequest.get();
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
                            *etag = Aws::Crt::String((const char *)value.ptr, value.len);
                        }
                    }
                };
                requestOptions.onStreamComplete = [signedRequest, keyPath, inputStream, etag, conn, completedCallback](
                                                      Http::HttpStream &stream, int error) {
                    int errorCode = error;

                    if (!errorCode)
                    {
                        errorCode = stream.GetResponseStatusCode() == 200 ? AWS_OP_SUCCESS : AWS_OP_ERR;

                        AWS_LOGF_INFO(
                            AWS_LS_COMMON_GENERAL,
                            "PutObjectInternal completed for path %s with response status %d.",
                            keyPath.c_str(),
                            stream.GetResponseStatusCode());
                    }
                    else
                    {
                        AWS_LOGF_INFO(
                            AWS_LS_COMMON_GENERAL,
                            "PutObjectInternal completed for path %s with error '%s'",
                            keyPath.c_str(),
                            aws_error_debug_str(errorCode));
                    }

                    completedCallback(errorCode, etag);
                };

                conn->NewClientStream(requestOptions);
            });
        });
}

void S3ObjectTransport::PutObjectMultipart(
    const Aws::Crt::String &key,
    std::uint64_t objectSize,
    MultipartTransferState::GetObjectPartCallback getObjectPart,
    MultipartTransferState::OnCompletedCallback onCompleted)
{
    CreateMultipartUpload(
        key,
        [this, key, objectSize, getObjectPart, onCompleted](int errorCode, std::shared_ptr<Aws::Crt::String> uploadId) {
            if (uploadId == nullptr || uploadId->empty())
            {
                errorCode = AWS_OP_ERR;
            }

            if (errorCode != AWS_OP_SUCCESS)
            {
                onCompleted(errorCode);
                return;
            }

            std::shared_ptr<MultipartTransferState> uploadState = std::make_shared<MultipartTransferState>(
                key, *uploadId, objectSize, GetNumParts(objectSize), getObjectPart, onCompleted);

            PushMultipartUpload(uploadState);

            UploadNextParts(0);
        });
}

void S3ObjectTransport::GetObject(
    const Aws::Crt::String &key,
    Aws::Crt::Http::OnIncomingBody onIncomingBody,
    TransportOpCompleted transportOpCompleted)
{
    auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
    request->AddHeader(m_hostHeader);

    request->SetMethod(aws_http_method_get);

    String keyPath = "/" + key;
    ByteCursor path = ByteCursorFromCString(keyPath.c_str());
    request->SetPath(path);

    Auth::AwsSigningConfig signingConfig(g_allocator);
    signingConfig.SetRegion(m_region);
    signingConfig.SetCredentialsProvider(m_credsProvider);
    signingConfig.SetService("s3");
    signingConfig.SetBodySigningType(Auth::BodySigningType::UnsignedPayload);
    signingConfig.SetSigningTimepoint(DateTime::Now());
    signingConfig.SetSigningAlgorithm(Auth::SigningAlgorithm::SigV4Header);

    m_signer->SignRequest(
        request,
        signingConfig,
        [this, onIncomingBody, transportOpCompleted](
            const std::shared_ptr<Aws::Crt::Http::HttpRequest> &signedRequest, int signingError) {
            if (signingError != AWS_OP_SUCCESS)
            {
                transportOpCompleted(signingError);
                return;
            }

            m_connManager->AcquireConnection([signedRequest, onIncomingBody, transportOpCompleted](
                                                 std::shared_ptr<Http::HttpClientConnection> conn, int connError) {
                if (connError != AWS_OP_SUCCESS)
                {
                    transportOpCompleted(connError);
                    return;
                }

                Http::HttpRequestOptions requestOptions;
                AWS_ZERO_STRUCT(requestOptions);
                requestOptions.request = signedRequest.get();
                requestOptions.onIncomingBody = onIncomingBody;
                requestOptions.onStreamComplete =
                    [signedRequest, conn, transportOpCompleted](Http::HttpStream &stream, int error) {
                        int errorCode = error;

                        if (!errorCode)
                        {
                            errorCode = stream.GetResponseStatusCode() == 200 ? AWS_OP_SUCCESS : AWS_OP_ERR;
                        }

                        transportOpCompleted(errorCode);
                    };

                conn->NewClientStream(requestOptions);
            });
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
    const Aws::Crt::String &key,
    CreateMultipartUploadCompleted completedCallback)
{
    auto createMultipartUploadRequest = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
    createMultipartUploadRequest->AddHeader(m_hostHeader);
    createMultipartUploadRequest->AddHeader(m_contentTypeHeader);
    createMultipartUploadRequest->SetMethod(aws_http_method_post);

    String keyPath = "/" + key + "?uploads";
    ByteCursor path = ByteCursorFromCString(keyPath.c_str());
    createMultipartUploadRequest->SetPath(path);

    Auth::AwsSigningConfig signingConfig(g_allocator);
    signingConfig.SetRegion(m_region);
    signingConfig.SetCredentialsProvider(m_credsProvider);
    signingConfig.SetService("s3");
    signingConfig.SetBodySigningType(Auth::BodySigningType::UnsignedPayload);
    signingConfig.SetSigningTimepoint(DateTime::Now());
    signingConfig.SetSigningAlgorithm(Auth::SigningAlgorithm::SigV4Header);

    AWS_LOGF_INFO(AWS_LS_COMMON_GENERAL, "Creating multipart upload for %s...", keyPath.c_str());

    std::shared_ptr<Aws::Crt::String> uploadId = std::make_shared<Aws::Crt::String>();

    m_signer->SignRequest(
        createMultipartUploadRequest,
        signingConfig,
        [this, uploadId, keyPath, completedCallback](
            const std::shared_ptr<Aws::Crt::Http::HttpRequest> &signedRequest, int signingError) {
            if (signingError != AWS_OP_SUCCESS)
            {
                completedCallback(signingError, nullptr);
                return;
            }

            m_connManager->AcquireConnection([uploadId, keyPath, signedRequest, completedCallback](
                                                 std::shared_ptr<Http::HttpClientConnection> conn, int connError) {
                if (connError != AWS_OP_SUCCESS)
                {
                    completedCallback(connError, nullptr);
                    return;
                }

                Http::HttpRequestOptions requestOptions;
                AWS_ZERO_STRUCT(requestOptions);
                requestOptions.request = signedRequest.get();
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
                requestOptions.onStreamComplete =
                    [uploadId, keyPath, signedRequest, conn, completedCallback](Http::HttpStream &stream, int error) {
                        int errorCode = error;

                        if (!errorCode)
                        {
                            errorCode = stream.GetResponseStatusCode() == 200 ? AWS_OP_SUCCESS : AWS_OP_ERR;

                            AWS_LOGF_INFO(
                                AWS_LS_COMMON_GENERAL,
                                "Created multipart upload for path %s with response status %d.",
                                keyPath.c_str(),
                                stream.GetResponseStatusCode());
                        }
                        else
                        {
                            AWS_LOGF_INFO(
                                AWS_LS_COMMON_GENERAL,
                                "Completed multipart upload for path %s with error '%s'",
                                keyPath.c_str(),
                                aws_error_debug_str(errorCode));
                        }

                        if (uploadId->empty())
                        {
                            errorCode = AWS_OP_ERR;
                        }

                        completedCallback(errorCode, uploadId);
                    };

                conn->NewClientStream(requestOptions);
            });
        });
}

void S3ObjectTransport::CompleteMultipartUpload(
    const Aws::Crt::String &key,
    const Aws::Crt::String &uploadId,
    const Aws::Crt::Vector<Aws::Crt::String> &etags,
    CompleteMultipartUploadCompleted completedCallback)
{
    AWS_LOGF_INFO(AWS_LS_COMMON_GENERAL, "Completing multipart upload for %s...", key.c_str());

    auto completeMultipartUploadRequest = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
    completeMultipartUploadRequest->AddHeader(m_hostHeader);
    completeMultipartUploadRequest->SetMethod(aws_http_method_post);

    std::shared_ptr<StringStream> xmlContents = std::make_shared<StringStream>();
    *xmlContents << "<?xml version=\"1.0\" encoding=\"UTF-8\" ?>\n";
    *xmlContents << "<CompleteMultipartUpload xmlns=\"http://s3.amazonaws.com/doc/2006-03-01/\">\n";

    for (int i = 0; i < etags.size(); ++i)
    {
        const Aws::Crt::String &etag = etags[i];
        int partNumber = i + 1;

        *xmlContents << "   <Part>\n";
        *xmlContents << "       <ETag>" << etag << "</ETag>\n";
        *xmlContents << "       <PartNumber>" << partNumber << "</PartNumber>\n";
        *xmlContents << "   </Part>\n";
    }

    *xmlContents << "</CompleteMultipartUpload>";

    aws_input_stream *inputStream = Aws::Crt::Io::AwsInputStreamNewCpp(xmlContents, aws_default_allocator());

    {
        int64_t streamLen = 0;
        aws_input_stream_get_length(inputStream, &streamLen);
        StringStream intValue;
        intValue << streamLen;
        String contentLengthVal = intValue.str();

        Http::HttpHeader contentLength;
        contentLength.name = ByteCursorFromCString("content-length");
        contentLength.value = ByteCursorFromCString(contentLengthVal.c_str());

        completeMultipartUploadRequest->AddHeader(contentLength);
    }

    aws_http_message_set_body_stream(completeMultipartUploadRequest->GetUnderlyingMessage(), inputStream);

    StringStream keyPathStream;
    keyPathStream << "/" << key << "?uploadId=" << uploadId;
    String keyPath = keyPathStream.str();
    ByteCursor path = ByteCursorFromCString(keyPath.c_str());
    completeMultipartUploadRequest->SetPath(path);

    Auth::AwsSigningConfig signingConfig(g_allocator);
    signingConfig.SetRegion(m_region);
    signingConfig.SetCredentialsProvider(m_credsProvider);
    signingConfig.SetService("s3");
    signingConfig.SetBodySigningType(Auth::BodySigningType::UnsignedPayload);
    signingConfig.SetSigningTimepoint(DateTime::Now());
    signingConfig.SetSigningAlgorithm(Auth::SigningAlgorithm::SigV4Header);

    m_signer->SignRequest(
        completeMultipartUploadRequest,
        signingConfig,
        [this, keyPath, completedCallback](
            const std::shared_ptr<Aws::Crt::Http::HttpRequest> &signedRequest, int signingError) {
            if (signingError != AWS_OP_SUCCESS)
            {
                completedCallback(signingError);
                return;
            }

            m_connManager->AcquireConnection([keyPath, signedRequest, completedCallback](
                                                 std::shared_ptr<Http::HttpClientConnection> conn, int connError) {
                if (connError != AWS_OP_SUCCESS)
                {
                    completedCallback(connError);
                    return;
                }

                Http::HttpRequestOptions requestOptions;
                AWS_ZERO_STRUCT(requestOptions);
                requestOptions.request = signedRequest.get();
                requestOptions.onStreamComplete =
                    [keyPath, signedRequest, conn, completedCallback](Http::HttpStream &stream, int error) {
                        int errorCode = error;

                        if (!errorCode)
                        {
                            errorCode = stream.GetResponseStatusCode() == 200 ? AWS_OP_SUCCESS : AWS_OP_ERR;

                            AWS_LOGF_INFO(
                                AWS_LS_COMMON_GENERAL,
                                "Completed multipart upload for path %s with response status %d.",
                                keyPath.c_str(),
                                stream.GetResponseStatusCode());
                        }
                        else
                        {
                            AWS_LOGF_INFO(
                                AWS_LS_COMMON_GENERAL,
                                "Completed multipart upload for path %s with error '%s'",
                                keyPath.c_str(),
                                aws_error_debug_str(errorCode));
                        }

                        completedCallback(errorCode);
                    };

                conn->NewClientStream(requestOptions);
            });
        });
}

void S3ObjectTransport::AbortMultipartUpload(
    const Aws::Crt::String &key,
    const Aws::Crt::String &uploadId,
    AbortMultipartUploadCompleted completedCallback)
{
    (void)uploadId;
    (void)completedCallback;

    AWS_LOGF_INFO(AWS_LS_COMMON_GENERAL, "Aborting multipart upload for %s...", key.c_str());

    auto abortMultipartUploadRequest = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
    abortMultipartUploadRequest->AddHeader(m_hostHeader);
    abortMultipartUploadRequest->SetMethod(aws_http_method_delete);

    String keyPath = "/" + key + "?uploadId=" + uploadId;
    ByteCursor keyPathByteCursor = ByteCursorFromCString(keyPath.c_str());
    abortMultipartUploadRequest->SetPath(keyPathByteCursor);

    Auth::AwsSigningConfig signingConfig(g_allocator);
    signingConfig.SetRegion(m_region);
    signingConfig.SetCredentialsProvider(m_credsProvider);
    signingConfig.SetService("s3");
    signingConfig.SetBodySigningType(Auth::BodySigningType::UnsignedPayload);
    signingConfig.SetSigningTimepoint(DateTime::Now());
    signingConfig.SetSigningAlgorithm(Auth::SigningAlgorithm::SigV4Header);

    m_signer->SignRequest(
        abortMultipartUploadRequest,
        signingConfig,
        [this, uploadId, keyPath, completedCallback](
            const std::shared_ptr<Aws::Crt::Http::HttpRequest> &signedRequest, int signingError) {
            if (signingError != AWS_OP_SUCCESS)
            {
                completedCallback(signingError);
                return;
            }

            m_connManager->AcquireConnection([uploadId, keyPath, signedRequest, completedCallback](
                                                 std::shared_ptr<Http::HttpClientConnection> conn, int connError) {
                if (connError != AWS_OP_SUCCESS)
                {
                    completedCallback(connError);
                    return;
                }

                Http::HttpRequestOptions requestOptions;
                AWS_ZERO_STRUCT(requestOptions);
                requestOptions.request = signedRequest.get();
                requestOptions.onStreamComplete =
                    [uploadId, keyPath, signedRequest, conn, completedCallback](Http::HttpStream &stream, int error) {
                        int errorCode = error;

                        if (!errorCode)
                        {
                            errorCode = stream.GetResponseStatusCode() == 204 ? AWS_OP_SUCCESS : AWS_OP_ERR;

                            AWS_LOGF_INFO(
                                AWS_LS_COMMON_GENERAL,
                                "Abort multipart upload for path %s with response status %d.",
                                keyPath.c_str(),
                                stream.GetResponseStatusCode());
                        }
                        else
                        {
                            AWS_LOGF_INFO(
                                AWS_LS_COMMON_GENERAL,
                                "Abort multipart upload for path %s with error '%s'",
                                keyPath.c_str(),
                                aws_error_debug_str(errorCode));
                        }

                        completedCallback(errorCode);
                    };

                conn->NewClientStream(requestOptions);
            });
        });
}

void S3ObjectTransport::UploadNextParts(uint32_t upStreamsReturning)
{
    UploadNextPartsForNextObject(upStreamsReturning);

    while (UploadNextPartsForNextObject(0))
    {
    }
}

bool S3ObjectTransport::UploadNextPartsForNextObject(uint32_t upStreamsReturning)
{
    uint32_t startPartIndex = 0;
    uint32_t numPartsToUpload = 0;
    std::shared_ptr<MultipartTransferState> uploadState;

    aws_mutex_lock(&m_upStreamsAvailableMutex);

    m_upStreamsAvailable += upStreamsReturning;

    if (m_upStreamsAvailable > 0)
    {
        bool searchingQueue = true;

        // Find the next thing in the queue that needs parts uploaded, cleaning up the queue along the way.
        while (searchingQueue)
        {
            uploadState = PeekMultipartUploadQueue();

            if (uploadState == nullptr)
            {
                searchingQueue = false;
            }
            else if (uploadState->GetPartsForUpload(m_upStreamsAvailable, startPartIndex, numPartsToUpload))
            {
                m_upStreamsAvailable -= numPartsToUpload;
                searchingQueue = false;
            }
            else
            {
                PopMultipartUploadQueue();
            }
        }
    }

    aws_mutex_unlock(&m_upStreamsAvailableMutex);

    for (uint32_t i = 0; i < numPartsToUpload; ++i)
    {
        uint32_t partIndex = startPartIndex + i;
        uint32_t partNumber = partIndex + 1;

        uint64_t partByteStart = partIndex * S3ObjectTransport::MaxPartSizeBytes;
        uint64_t partByteRemainder = uploadState->GetObjectSize() - (partIndex * S3ObjectTransport::MaxPartSizeBytes);
        uint64_t partByteSize = min(partByteRemainder, S3ObjectTransport::MaxPartSizeBytes);

        aws_input_stream *inputStream = uploadState->GetObjectPart(partByteStart, partByteSize);

        StringStream keyPathStream;
        keyPathStream << uploadState->GetKey() << "?partNumber=" << partNumber
                      << "&uploadId=" << uploadState->GetUploadId();

        // Do the actual uploading of the part
        PutObject(
            keyPathStream.str(),
            inputStream,
            (uint32_t)EPutObjectFlags::RetrieveETag,
            [this, uploadState, partIndex](int errorCode, std::shared_ptr<Aws::Crt::String> etag) {
                if (errorCode == AWS_OP_SUCCESS && etag != nullptr)
                {
                    uploadState->SetETag(partIndex, *etag);

                    if (uploadState->IncNumPartsCompleted())
                    {
                        Aws::Crt::Vector<Aws::Crt::String> etags;
                        uploadState->GetETags(etags);

                        CompleteMultipartUpload(
                            uploadState->GetKey(),
                            uploadState->GetUploadId(),
                            etags,
                            [uploadState](int errorCode) { uploadState->SetCompleted(errorCode); });
                    }
                }
                else
                {
                    AbortMultipartUpload(
                        uploadState->GetKey(), uploadState->GetUploadId(), [](int errorCode) { (void)errorCode; });

                    uploadState->SetCompleted(errorCode);
                }

                UploadNextParts(1);
            });
    }

    return numPartsToUpload > 0;
}

void S3ObjectTransport::PushMultipartUpload(std::shared_ptr<MultipartTransferState> uploadState)
{
    aws_mutex_lock(&m_multipartUploadQueueMutex);
    m_multipartUploadQueue.push(uploadState);
    aws_mutex_unlock(&m_multipartUploadQueueMutex);
}

std::shared_ptr<MultipartTransferState> S3ObjectTransport::PeekMultipartUploadQueue()
{
    std::shared_ptr<MultipartTransferState> uploadState;

    aws_mutex_lock(&m_multipartUploadQueueMutex);

    if (m_multipartUploadQueue.size() > 0)
    {
        uploadState = m_multipartUploadQueue.front();
    }

    aws_mutex_unlock(&m_multipartUploadQueueMutex);

    return uploadState;
}

void S3ObjectTransport::PopMultipartUploadQueue()
{
    aws_mutex_lock(&m_multipartUploadQueueMutex);
    m_multipartUploadQueue.pop();
    aws_mutex_unlock(&m_multipartUploadQueueMutex);
}
