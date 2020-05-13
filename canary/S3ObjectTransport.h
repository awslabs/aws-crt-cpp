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
#pragma once

#include <aws/common/condition_variable.h>
#include <aws/common/mutex.h>
#include <aws/crt/DateTime.h>
#include <aws/crt/auth/Sigv4Signing.h>
#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/io/Stream.h>

#include <queue>
#include <set>

#include "MultipartTransferState.h"
#include "TransferState.h"

class CanaryApp;
struct aws_allocator;
struct aws_event_loop;

enum class EPutObjectFlags : uint32_t
{
    RetrieveETag = 0x00000001
};

using GetObjectFinished = std::function<void(int32_t errorCode)>;
using PutObjectFinished = std::function<void(int32_t errorCode, std::shared_ptr<Aws::Crt::String> etag)>;

using PutObjectMultipartFinished = std::function<void(int32_t errorCode, uint32_t numParts)>;
using GetObjectMultipartFinished = std::function<void(int32_t errorCode)>;

using GetPartStream =
    std::function<std::shared_ptr<Aws::Crt::Io::InputStream>(const std::shared_ptr<TransferState> &transferState)>;
using ReceivePartCallback =
    std::function<void(const std::shared_ptr<TransferState> &transferState, const Aws::Crt::ByteCursor &data)>;

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            class EndPointMonitorManager;
        }
    } // namespace Crt
} // namespace Aws

/*
 * Makes available a handful of S3 operations invoked by using the REST API to a specific bucket.  Also had
 * functionality for resolving a number of DNS addresses that transfers can be distributed across.
 */
class S3ObjectTransport
{
  public:
    S3ObjectTransport(
        CanaryApp &canaryApp,
        const Aws::Crt::String &bucket,
        uint64_t minThroughputBytesPerSecond = 0ULL);

    /*
     * Returns the endpoint of the bucket being used.
     */
    const Aws::Crt::String &GetEndpoint() const { return m_endpoint; }

    /*
     * Upload a single part object, or a part of an object.
     */
    void PutObject(
        const std::shared_ptr<TransferState> &transferState,
        const Aws::Crt::String &key,
        const std::shared_ptr<Aws::Crt::Io::InputStream> &inputStream,
        std::uint32_t flags,
        const PutObjectFinished &finishedCallback);

    /*
     * Get a single part object, or a part of an object.
     */
    void GetObject(
        const std::shared_ptr<TransferState> &transferState,
        const Aws::Crt::String &key,
        std::uint32_t partNumber,
        Aws::Crt::Http::OnIncomingBody onIncomingBody,
        const GetObjectFinished &getObjectFinished);

    /*
     * Upload a multipart object.
     */
    std::shared_ptr<MultipartUploadState> PutObjectMultipart(
        const Aws::Crt::String &key,
        std::uint64_t objectSize,
        std::uint32_t numParts,
        const GetPartStream &getPartStream,
        const PutObjectMultipartFinished &finishedCallback);

    /*
     * Download a multipart object.
     */
    std::shared_ptr<MultipartDownloadState> GetObjectMultipart(
        const Aws::Crt::String &key,
        std::uint32_t numParts,
        const ReceivePartCallback &receivePart,
        const GetObjectMultipartFinished &finishedCallback);

    /*
     * Given a number of transfers, resolve the appropriate amount of DNS addresses.
     */
    void WarmDNSCache(uint32_t numTransfers, uint32_t transfersPerAddress);

  private:
    using SignedRequestCallback =
        std::function<void(const std::shared_ptr<Aws::Crt::Http::HttpClientConnection> &conn, int32_t errorCode)>;
    using CreateMultipartUploadFinished = std::function<void(int32_t error, const Aws::Crt::String &uploadId)>;
    using CompleteMultipartUploadFinished = std::function<void(int32_t error)>;
    using AbortMultipartUploadFinished = std::function<void(int32_t error)>;
    using AcquireConnManagerCallback = std::function<void(
        const std::shared_ptr<Aws::Crt::Http::HttpClientConnectionManager> &connManager,
        const Aws::Crt::String &address)>;

    CanaryApp &m_canaryApp;
    const Aws::Crt::String m_bucketName;
    Aws::Crt::Http::HttpHeader m_hostHeader;
    Aws::Crt::Http::HttpHeader m_contentTypeHeader;
    Aws::Crt::String m_endpoint;

    uint32_t m_transfersPerAddress;

    std::shared_ptr<Aws::Crt::Http::HttpClientConnectionManager> m_connManager;
    std::shared_ptr<Aws::Crt::Io::EndPointMonitorManager> m_endPointMonitorManager;

    std::atomic<uint32_t> m_activeRequestsCount;

    uint64_t m_minThroughputBytes;

    void EmitS3AddressCountMetric(size_t addressCount);

    void MakeSignedRequest(
        const std::shared_ptr<Aws::Crt::Http::HttpRequest> &request,
        const Aws::Crt::Http::HttpRequestOptions &requestOptions,
        SignedRequestCallback callback);

    void MakeSignedRequest_SendRequest(
        const std::shared_ptr<Aws::Crt::Http::HttpClientConnection> &conn,
        const Aws::Crt::Http::HttpRequestOptions &requestOptions,
        const std::shared_ptr<Aws::Crt::Http::HttpRequest> &signedRequest);

    void AddContentLengthHeader(
        const std::shared_ptr<Aws::Crt::Http::HttpRequest> &request,
        const std::shared_ptr<Aws::Crt::Io::InputStream> &body);

    void UploadNextPart(
        const std::shared_ptr<MultipartUploadState> &state,
        const GetPartStream &getPartStream,
        const PutObjectMultipartFinished &finishedCallback);

    void GetNextPart(
        const std::shared_ptr<MultipartDownloadState> &multipartState,
        const ReceivePartCallback &receiveObjectPartData,
        const GetObjectMultipartFinished &finishedCallback);

    void CreateMultipartUpload(const Aws::Crt::String &key, const CreateMultipartUploadFinished &finishedCallback);

    void CompleteMultipartUpload(
        const Aws::Crt::String &key,
        const Aws::Crt::String &uploadId,
        const Aws::Crt::Vector<Aws::Crt::String> &etags,
        const CompleteMultipartUploadFinished &finishedCallback);

    void AbortMultipartUpload(
        const Aws::Crt::String &key,
        const Aws::Crt::String &uploadId,
        const AbortMultipartUploadFinished &finishedCallback);
};
