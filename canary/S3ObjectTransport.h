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
#pragma once

#include <aws/common/condition_variable.h>
#include <aws/common/mutex.h>
#include <aws/crt/DateTime.h>
#include <aws/crt/auth/Sigv4Signing.h>
#include <aws/crt/http/HttpConnectionManager.h>

#include <queue>

#include "MultipartTransferProcessor.h"
#include "MultipartTransferState.h"

using GetObjectCompleted = std::function<void(int32_t errorCode)>;
using PutObjectCompleted = std::function<void(int32_t errorCode, std::shared_ptr<Aws::Crt::String> etag)>;

using PutObjectMultipartCompleted = std::function<void(int32_t errorCode, uint32_t numParts)>;
using GetObjectMultipartCompleted = std::function<void(int32_t errorCode)>;

using GetObjectPartCallback =
    std::function<struct aws_input_stream *(const MultipartTransferState::PartInfo &partInfo)>;
using ReceiveObjectPartDataCallback =
    std::function<void(const MultipartTransferState::PartInfo &partInfo, const Aws::Crt::ByteCursor &data)>;

struct aws_allocator;
struct aws_event_loop;

enum class EPutObjectFlags : uint32_t
{
    RetrieveETag = 0x00000001
};

class S3ObjectTransport
{
  public:
    static const uint64_t MaxPartSizeBytes;
    static const uint32_t MaxStreams;
    static const int32_t S3GetObjectResponseStatus_PartialContent;

    S3ObjectTransport(
        const Aws::Crt::String &region,
        const Aws::Crt::String &bucket,
        Aws::Crt::Io::TlsContext &tlsContext,
        Aws::Crt::Io::ClientBootstrap &clientBootstrap,
        const std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> &credsProvider,
        const std::shared_ptr<Aws::Crt::Auth::Sigv4HttpRequestSigner> &signer,
        size_t maxCons = 1000);

    void PutObject(
        const Aws::Crt::String &key,
        struct aws_input_stream *inputStream,
        uint32_t flags,
        PutObjectCompleted onCompleted);

    uint32_t PutObjectMultipart(
        const Aws::Crt::String &key,
        std::uint64_t objectSize,
        GetObjectPartCallback getObjectPart,
        PutObjectMultipartCompleted onCompleted);

    void GetObject(
        const Aws::Crt::String &key,
        std::uint32_t partNumber,
        Aws::Crt::Http::OnIncomingBody onIncomingBody,
        GetObjectCompleted getObjectCompleted);

    void GetObjectMultipart(
        const Aws::Crt::String &key,
        std::uint32_t numParts,
        ReceiveObjectPartDataCallback receiveObjectPart,
        GetObjectMultipartCompleted onCompleted);

  private:
    using CreateMultipartUploadCompleted =
        std::function<void(int32_t error, std::shared_ptr<Aws::Crt::String> uploadId)>;
    using CompleteMultipartUploadCompleted = std::function<void(int32_t error)>;
    using AbortMultipartUploadCompleted = std::function<void(int32_t error)>;

    std::shared_ptr<Aws::Crt::Http::HttpClientConnectionManager> m_connManager;
    std::shared_ptr<Aws::Crt::Auth::Sigv4HttpRequestSigner> m_signer;
    std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> m_credsProvider;
    const Aws::Crt::String m_region;
    const Aws::Crt::String m_bucketName;
    Aws::Crt::Http::HttpHeader m_hostHeader;
    Aws::Crt::Http::HttpHeader m_contentTypeHeader;
    Aws::Crt::String m_endpoint;

    MultipartTransferProcessor m_uploadProcessor;
    MultipartTransferProcessor m_downloadProcessor;

    uint32_t GetNumParts(uint64_t objectSize) const;

    void CreateMultipartUpload(const Aws::Crt::String &key, CreateMultipartUploadCompleted completedCallback);

    void CompleteMultipartUpload(
        const Aws::Crt::String &key,
        const Aws::Crt::String &uploadId,
        const Aws::Crt::Vector<Aws::Crt::String> &etags,
        CompleteMultipartUploadCompleted completedCallback);

    void AbortMultipartUpload(
        const Aws::Crt::String &key,
        const Aws::Crt::String &uploadId,
        AbortMultipartUploadCompleted completedCallback);

    void UploadPart(
        const std::shared_ptr<MultipartTransferState> &state,
        const std::shared_ptr<Aws::Crt::String> &uploadId,
        const MultipartTransferState::PartInfo &partInfo,
        aws_input_stream *partInputStream,
        const MultipartTransferState::PartFinishedCallback &partFinished);
};
