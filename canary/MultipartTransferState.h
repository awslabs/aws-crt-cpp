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
#include <aws/crt/Types.h>
#include <mutex>

class S3ObjectTransport;

class MultipartTransferState
{
  public:
    struct PartInfo
    {
        uint32_t index;
        uint32_t number;
        uint64_t byteOffset;
        uint64_t byteSize;

        PartInfo();
        PartInfo(uint32_t partIndex, uint32_t partNumber, uint64_t partByteOffset, uint64_t partByteSize);
    };

    using PartFinishedCallback = std::function<void()>;
    using ProcessPartCallback = std::function<
        void(std::shared_ptr<MultipartTransferState> state, const PartInfo &partInfo, PartFinishedCallback callback)>;
    using OnCompletedCallback = std::function<void(int32_t errorCode)>;

    MultipartTransferState(
        const Aws::Crt::String &key,
        uint64_t objectSize,
        uint32_t numParts,
        ProcessPartCallback processPartCallback,
        OnCompletedCallback onCompletedCallback);

    ~MultipartTransferState();

    void SetCompleted(int32_t errorCode = AWS_OP_SUCCESS);
    void SetETag(uint32_t partIndex, const Aws::Crt::String &etag);

    bool GetPartRangeForTransfer(uint32_t desiredNumParts, uint32_t &outStartPartIndex, uint32_t &outNumParts);
    bool IncNumPartsCompleted();

    bool IsCompleted() const;
    const Aws::Crt::String &GetKey() const;
    void GetETags(Aws::Crt::Vector<Aws::Crt::String> &outETags);
    uint32_t GetNumParts() const;
    uint32_t GetNumPartsRequested() const;
    uint32_t GetNumPartsCompleted() const;
    uint64_t GetObjectSize() const;

    template <typename... TArgs> void ProcessPart(TArgs &&... Args) const
    {
        m_processPartCallback(std::forward<TArgs>(Args)...);
    }

  private:
    int32_t m_errorCode;
    uint32_t m_numParts;
    std::atomic<bool> m_isCompleted;
    std::atomic<uint32_t> m_numPartsRequested;
    std::atomic<uint32_t> m_numPartsCompleted;
    uint64_t m_objectSize;
    Aws::Crt::String m_key;
    Aws::Crt::Vector<Aws::Crt::String> m_etags;
    aws_mutex m_etagsMutex;
    ProcessPartCallback m_processPartCallback;
    OnCompletedCallback m_onCompletedCallback;
};