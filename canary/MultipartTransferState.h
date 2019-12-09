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

#include <atomic>
#include <aws/common/mutex.h>
#include <aws/common/string.h>
#include <aws/crt/types.h>
#include <mutex>

class S3ObjectTransport;

class MultipartTransferState
{
  public:
    using GetObjectPartCallback =
        std::function<struct aws_input_stream *(uint64_t partStartBytes, uint64_t partSizeBytes)>;
    using OnCompletedCallback = std::function<void(int32_t errorCode)>;

    MultipartTransferState(
        const Aws::Crt::String &key,
        const Aws::Crt::String &uploadId,
        uint64_t objectSize,
        uint32_t numParts,
        GetObjectPartCallback getObjectPartCallback,
        OnCompletedCallback onCompletedCallback);

    ~MultipartTransferState();

    void SetCompleted(int32_t errorCode = AWS_OP_SUCCESS);
    void SetETag(uint32_t partIndex, const Aws::Crt::String &etag);

    bool GetPartsForUpload(uint32_t desiredNumParts, uint32_t &outStartPartIndex, uint32_t &outNumParts);
    bool IncNumPartsCompleted();

    bool IsCompleted() const;
    const Aws::Crt::String &GetKey() const;
    const Aws::Crt::String &GetUploadId() const;
    void GetETags(Aws::Crt::Vector<Aws::Crt::String> &outETags);
    uint32_t GetNumParts() const;
    uint32_t GetNumPartsRequested() const;
    uint32_t GetNumPartsCompleted() const;
    uint64_t GetObjectSize() const;


    template <typename... TArgs> aws_input_stream *GetObjectPart(TArgs &&... Args) const
    {
        return m_getObjectPartCallback(std::forward<TArgs>(Args)...);
    }

  private:
    uint32_t m_isCompleted : 1;
    int32_t m_errorCode;
    uint32_t m_numParts;
    std::atomic<uint32_t> m_numPartsRequested;
    std::atomic<uint32_t> m_numPartsCompleted;
    uint64_t m_objectSize;
    Aws::Crt::String m_key;
    Aws::Crt::String m_uploadId;
    Aws::Crt::Vector<Aws::Crt::String> m_etags;
    aws_mutex m_completionMutex;
    aws_mutex m_etagsMutex;
    GetObjectPartCallback m_getObjectPartCallback;
    OnCompletedCallback m_onCompletedCallback;
};