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
#include <aws/crt/io/Stream.h>

#include <queue>
#include <set>

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

using SendPartCallback =
    std::function<std::shared_ptr<Aws::Crt::Io::InputStream>(const std::shared_ptr<TransferState> &transferState)>;
using ReceivePartCallback =
    std::function<void(const std::shared_ptr<TransferState> &transferState, const Aws::Crt::ByteCursor &data)>;

class S3ObjectTransport
{
  public:
    static const uint32_t MaxStreams;
    static const uint32_t TransfersPerAddress;
    static const int32_t S3GetObjectResponseStatus_PartialContent;

    S3ObjectTransport(CanaryApp &canaryApp, const Aws::Crt::String &bucket);

    void PutObject(
        const Aws::Crt::String &key,
        const std::shared_ptr<Aws::Crt::Io::InputStream> &inputStream,
        std::uint32_t flags,
        const PutObjectFinished &finishedCallback);

    void GetObject(
        const Aws::Crt::String &key,
        std::uint32_t partNumber,
        Aws::Crt::Http::OnIncomingBody onIncomingBody,
        const GetObjectFinished &getObjectFinished);

    size_t GetOpenConnectionCount();

    size_t GetUniqueEndpointsUsedCount() const { return m_uniqueEndpointsUsed.size(); }

    const Aws::Crt::String &GetEndpoint() const { return m_endpoint; }

    void WarmDNSCache(uint32_t numTransfers);

    void SpawnConnectionManagers();

    const Aws::Crt::String &GetAddressForTransfer(uint32_t index);

    void SeedAddressCache(const Aws::Crt::String &address);

  private:

    using SignedRequestCallback =
        std::function<void(std::shared_ptr<Aws::Crt::Http::HttpClientConnection> conn, int32_t errorCode)>;

    CanaryApp &m_canaryApp;
    const Aws::Crt::String m_bucketName;
    Aws::Crt::Http::HttpHeader m_hostHeader;
    Aws::Crt::Http::HttpHeader m_contentTypeHeader;
    Aws::Crt::String m_endpoint;

    std::vector<std::shared_ptr<Aws::Crt::Http::HttpClientConnectionManager>> m_connManagerTrashCan;
    std::vector<std::shared_ptr<Aws::Crt::Http::HttpClientConnectionManager>> m_connManagers;
    std::vector<Aws::Crt::String> m_addressCache;

    std::atomic<uint32_t> m_connManagersUseCount;
    std::atomic<uint32_t> m_activeRequestsCount;

    std::set<Aws::Crt::String> m_uniqueEndpointsUsed;

    std::shared_ptr<Aws::Crt::Http::HttpClientConnectionManager> GetNextConnManager();

    void MakeSignedRequest(
        const std::shared_ptr<Aws::Crt::Http::HttpRequest> &request,
        const Aws::Crt::Http::HttpRequestOptions &requestOptions,
        SignedRequestCallback callback);

    void MakeSignedRequest_SendRequest(
        const std::shared_ptr<Aws::Crt::Http::HttpClientConnection> &conn,
        const Aws::Crt::Http::HttpRequestOptions &requestOptions,
        const std::shared_ptr<Aws::Crt::Http::HttpRequest> &signedRequest);

    void AddContentLengthHeader(
        std::shared_ptr<Aws::Crt::Http::HttpRequest> request,
        const std::shared_ptr<Aws::Crt::Io::InputStream> &body);
};
