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

const uint64_t S3ObjectTransport::MaxPartSizeBytes = 10 * 1000 * 1000;
const uint32_t S3ObjectTransport::MaxStreams = 10;
const int32_t S3ObjectTransport::S3GetObjectResponseStatus_PartialContent = 206;

S3ObjectTransport::S3ObjectTransport(
    const Aws::Crt::String &region,
    const Aws::Crt::String &bucket,
    Aws::Crt::Io::TlsContext &tlsContext,
    Aws::Crt::Io::ClientBootstrap &clientBootstrap,
    const std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> &credsProvider,
    const std::shared_ptr<Aws::Crt::Auth::Sigv4HttpRequestSigner> &signer,
    size_t maxCons)
    : m_signer(signer), m_credsProvider(credsProvider), m_region(region), m_bucketName(bucket),
      m_uploadProcessor(S3ObjectTransport::MaxStreams), m_downloadProcessor(S3ObjectTransport::MaxStreams)
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

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "PutObject initiated for path %s...", keyPath.c_str());

    std::shared_ptr<Aws::Crt::String> etag = nullptr;

    if ((flags & (uint32_t)EPutObjectFlags::RetrieveETag) != 0)
    {
        etag = std::make_shared<Aws::Crt::String>();
    }

    m_signer->SignRequest(
        request,
        signingConfig,
        [this, keyPath, etag, completedCallback](
            const std::shared_ptr<Aws::Crt::Http::HttpRequest> &signedRequest, int signingError) {
            if (signingError != AWS_OP_SUCCESS)
            {
                completedCallback(signingError, nullptr);
                return;
            }

            m_connManager->AcquireConnection([signedRequest, keyPath, etag, completedCallback](
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
                requestOptions.onStreamComplete =
                    [signedRequest, keyPath, etag, conn, completedCallback](Http::HttpStream &stream, int error) {
                        int errorCode = error;

                        if (!errorCode)
                        {
                            errorCode = (stream.GetResponseStatusCode() == 200) ? AWS_OP_SUCCESS : AWS_OP_ERR;

                            aws_log_level logLevel = (errorCode == AWS_OP_ERR) ? AWS_LL_ERROR : AWS_LL_INFO;

                            AWS_LOGF(
                                logLevel,
                                AWS_LS_CRT_CPP_CANARY,
                                "PutObject completed for path %s with response status %d on thread %" PRId64,
                                keyPath.c_str(),
                                stream.GetResponseStatusCode(),
                                aws_thread_current_thread_id());
                        }
                        else
                        {
                            AWS_LOGF_ERROR(
                                AWS_LS_CRT_CPP_CANARY,
                                "PutObject completed for path %s with error '%s' on thread %" PRId64,
                                keyPath.c_str(),
                                aws_error_debug_str(errorCode),
                                aws_thread_current_thread_id());
                        }

                        completedCallback(errorCode, etag);
                    };

                conn->NewClientStream(requestOptions);
            });
        });
}

uint32_t S3ObjectTransport::PutObjectMultipart(
    const Aws::Crt::String &key,
    std::uint64_t objectSize,
    GetObjectPartCallback getObjectPart,
    PutObjectMultipartCompleted onCompleted)
{
    uint32_t numParts = GetNumParts(objectSize);

    CreateMultipartUpload(
        key,
        [this, key, objectSize, numParts, getObjectPart, onCompleted](
            int errorCode, std::shared_ptr<Aws::Crt::String> uploadId) {
            if (uploadId == nullptr || uploadId->empty())
            {
                errorCode = AWS_OP_ERR;
            }

            if (errorCode != AWS_OP_SUCCESS)
            {
                onCompleted(errorCode, numParts);
                return;
            }

            std::shared_ptr<MultipartTransferState> uploadState = std::make_shared<MultipartTransferState>(
                key,
                objectSize,
                numParts,
                [this, uploadId, getObjectPart, onCompleted](
                    std::shared_ptr<MultipartTransferState> state,
                    const MultipartTransferState::PartInfo &partInfo,
                    MultipartTransferState::PartFinishedCallback partFinished) {
                    aws_input_stream *partInputStream = getObjectPart(partInfo);
                    UploadPart(state, uploadId, partInfo, partInputStream, partFinished);
                },
                [numParts, onCompleted](int32_t errorCode) { onCompleted(errorCode, numParts); });

            m_uploadProcessor.PushQueue(uploadState);
        });

    return numParts;
}

void S3ObjectTransport::UploadPart(
    const std::shared_ptr<MultipartTransferState> &state,
    const std::shared_ptr<Aws::Crt::String> &uploadId,
    const MultipartTransferState::PartInfo &partInfo,
    aws_input_stream *partInputStream,
    const MultipartTransferState::PartFinishedCallback &partFinished)
{
    StringStream keyPathStream;
    keyPathStream << state->GetKey() << "?partNumber=" << partInfo.number << "&uploadId=" << *uploadId;

    PutObject(
        keyPathStream.str(),
        partInputStream,
        (uint32_t)EPutObjectFlags::RetrieveETag,
        [this, uploadId, state, partInfo, partFinished](int errorCode, std::shared_ptr<Aws::Crt::String> etag) {
            if (errorCode == AWS_OP_SUCCESS && etag != nullptr)
            {
                state->SetETag(partInfo.index, *etag);

                if (state->IncNumPartsCompleted())
                {
                    Aws::Crt::Vector<Aws::Crt::String> etags;
                    state->GetETags(etags);

                    CompleteMultipartUpload(
                        state->GetKey(), *uploadId, etags, [this, state, uploadId](int32_t errorCode) {
                            if (errorCode != AWS_OP_SUCCESS)
                            {
                                AbortMultipartUpload(
                                    state->GetKey(), *uploadId, [](int32_t errorCode) { (void)errorCode; });
                            }
                            state->SetCompleted(errorCode);
                        });
                }
            }
            else
            {
                AbortMultipartUpload(state->GetKey(), *uploadId, [](int32_t errorCode) { (void)errorCode; });

                state->SetCompleted(errorCode);
            }

            AWS_LOGF_INFO(
                AWS_LS_CRT_CPP_CANARY,
                "UploadPart for path %s and part #%d (%d/%d) just returned code %d",
                state->GetKey().c_str(),
                partInfo.number,
                state->GetNumPartsCompleted(),
                state->GetNumParts(),
                errorCode);

            partFinished();
        });
}

void S3ObjectTransport::GetObject(
    const Aws::Crt::String &key,
    uint32_t partNumber,
    Aws::Crt::Http::OnIncomingBody onIncomingBody,
    GetObjectCompleted getObjectCompleted)
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
        [this, keyPath, partNumber, onIncomingBody, getObjectCompleted](
            const std::shared_ptr<Aws::Crt::Http::HttpRequest> &signedRequest, int signingError) {
            if (signingError != AWS_OP_SUCCESS)
            {
                getObjectCompleted(signingError);
                return;
            }

            m_connManager->AcquireConnection([signedRequest, keyPath, partNumber, onIncomingBody, getObjectCompleted](
                                                 std::shared_ptr<Http::HttpClientConnection> conn, int connError) {
                if (connError != AWS_OP_SUCCESS)
                {
                    getObjectCompleted(connError);
                    return;
                }

                Http::HttpRequestOptions requestOptions;
                AWS_ZERO_STRUCT(requestOptions);
                requestOptions.request = signedRequest.get();
                requestOptions.onIncomingBody = onIncomingBody;
                requestOptions.onStreamComplete = [conn, signedRequest, keyPath, partNumber, getObjectCompleted](
                                                      Http::HttpStream &stream, int error) {
                    int errorCode = error;

                    if (!errorCode)
                    {
                        int32_t successStatus = partNumber > 0 ? S3GetObjectResponseStatus_PartialContent : 200;

                        errorCode = (stream.GetResponseStatusCode() == successStatus) ? AWS_OP_SUCCESS : AWS_OP_ERR;

                        aws_log_level logLevel = (errorCode == AWS_OP_ERR) ? AWS_LL_ERROR : AWS_LL_INFO;

                        AWS_LOGF(
                            logLevel,
                            AWS_LS_CRT_CPP_CANARY,
                            "GetObject completed for path %s with response status %d on thread %" PRId64,
                            keyPath.c_str(),
                            stream.GetResponseStatusCode(),
                            aws_thread_current_thread_id());
                    }
                    else
                    {
                        AWS_LOGF_ERROR(
                            AWS_LS_CRT_CPP_CANARY,
                            "GetObject completed for path %s with error '%s' on thread %" PRId64,
                            keyPath.c_str(),
                            aws_error_debug_str(errorCode),
                            aws_thread_current_thread_id());
                    }

                    getObjectCompleted(errorCode);
                };

                conn->NewClientStream(requestOptions);
            });
        });
}

void S3ObjectTransport::GetObjectMultipart(
    const Aws::Crt::String &key,
    std::uint32_t numParts,
    ReceiveObjectPartDataCallback receiveObjectPartData,
    GetObjectMultipartCompleted onCompleted)
{
    std::shared_ptr<MultipartTransferState> downloadState = std::make_shared<MultipartTransferState>(
        key,
        0L,
        numParts,
        [this, key, receiveObjectPartData](
            std::shared_ptr<MultipartTransferState> state,
            const MultipartTransferState::PartInfo &partInfo,
            MultipartTransferState::PartFinishedCallback partFinished) {
            GetObject(
                key,
                partInfo.number,
                [state, partInfo, receiveObjectPartData](Http::HttpStream &stream, const ByteCursor &data) {
                    (void)stream;

                    receiveObjectPartData(partInfo, data);
                },
                [key, state, partInfo, partFinished](int32_t errorCode) {
                    if (errorCode != AWS_OP_SUCCESS)
                    {
                        AWS_LOGF_ERROR(
                            AWS_LS_CRT_CPP_CANARY, "Did not receive part #%d for %s", partInfo.number, key.c_str());

                        state->SetCompleted(errorCode);
                    }
                    else
                    {
                        AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Received part #%d for %s", partInfo.number, key.c_str());

                        if (state->IncNumPartsCompleted())
                        {
                            AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "All parts received for %s", key.c_str());
                            state->SetCompleted(AWS_OP_SUCCESS);
                        }
                    }

                    partFinished();
                });
        },
        [onCompleted](int32_t errorCode) { onCompleted(errorCode); });

    m_downloadProcessor.PushQueue(downloadState);
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

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Creating multipart upload for %s...", keyPath.c_str());

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
                            errorCode = (stream.GetResponseStatusCode() == 200) ? AWS_OP_SUCCESS : AWS_OP_ERR;

                            aws_log_level logLevel = (errorCode == AWS_OP_ERR) ? AWS_LL_ERROR : AWS_LL_INFO;

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
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Completing multipart upload for %s...", key.c_str());

    auto completeMultipartUploadRequest = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
    completeMultipartUploadRequest->AddHeader(m_hostHeader);
    completeMultipartUploadRequest->SetMethod(aws_http_method_post);

    std::shared_ptr<StringStream> xmlContents = std::make_shared<StringStream>();
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
                            errorCode = (stream.GetResponseStatusCode() == 200) ? AWS_OP_SUCCESS : AWS_OP_ERR;

                            aws_log_level logLevel = (errorCode == AWS_OP_ERR) ? AWS_LL_ERROR : AWS_LL_INFO;

                            AWS_LOGF(
                                logLevel,
                                AWS_LS_CRT_CPP_CANARY,
                                "Completed multipart upload for path %s with response status %d.",
                                keyPath.c_str(),
                                stream.GetResponseStatusCode());
                        }
                        else
                        {
                            AWS_LOGF_ERROR(
                                AWS_LS_CRT_CPP_CANARY,
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
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Aborting multipart upload for %s...", key.c_str());

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
                            errorCode = (stream.GetResponseStatusCode() == 204) ? AWS_OP_SUCCESS : AWS_OP_ERR;

                            aws_log_level logLevel = (errorCode == AWS_OP_ERR) ? AWS_LL_ERROR : AWS_LL_INFO;

                            AWS_LOGF(
                                logLevel,
                                AWS_LS_CRT_CPP_CANARY,
                                "Abort multipart upload for path %s with response status %d.",
                                keyPath.c_str(),
                                stream.GetResponseStatusCode());
                        }
                        else
                        {
                            AWS_LOGF_ERROR(
                                AWS_LS_CRT_CPP_CANARY,
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
