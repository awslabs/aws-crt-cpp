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

#include "MultipartTransferState.h"
#include "S3ObjectTransport.h"

#include <aws/crt/Api.h>
#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/Stream.h>
#include <aws/io/stream.h>

using namespace Aws::Crt;

MultipartTransferState::PartInfo::PartInfo() : index(0), number(0), byteOffset(0), byteSize(0) {}
MultipartTransferState::PartInfo::PartInfo(
    uint32_t partIndex,
    uint32_t partNumber,
    uint64_t partByteOffset,
    uint64_t partByteSize)
    : index(partIndex), number(partNumber), byteOffset(partByteOffset), byteSize(partByteSize)
{
}

MultipartTransferState::MultipartTransferState(
    const Aws::Crt::String &key,
    uint64_t objectSize,
    uint32_t numParts,
    ProcessPartCallback processPartCallback,
    OnCompletedCallback onCompletedCallback)
{
    m_isCompleted = false;
    m_errorCode = AWS_OP_SUCCESS;
    m_numParts = numParts;
    m_numPartsRequested = 0;
    m_numPartsCompleted = 0;
    m_objectSize = objectSize;
    m_key = key;
    m_processPartCallback = processPartCallback;
    m_onCompletedCallback = onCompletedCallback;

    aws_mutex_init(&m_etagsMutex);
}

MultipartTransferState::~MultipartTransferState()
{
    aws_mutex_clean_up(&m_etagsMutex);
}

void MultipartTransferState::SetCompleted(int32_t errorCode)
{
    bool wasCompleted = m_isCompleted.exchange(true);

    if (wasCompleted)
    {
        AWS_LOGF_INFO(
            AWS_LS_CRT_CPP_CANARY,
            "MultipartTransferState::SetCompleted being called multiple times--not recording error code %d.",
            errorCode);
    }
    else
    {
        m_errorCode = errorCode;
        m_onCompletedCallback(m_errorCode);
    }
}

bool MultipartTransferState::IsCompleted() const
{
    return m_isCompleted;
}

void MultipartTransferState::SetETag(uint32_t partIndex, const Aws::Crt::String &etag)
{
    AWS_FATAL_ASSERT(partIndex < m_numParts);

    aws_mutex_lock(&m_etagsMutex);
    while (m_etags.size() <= partIndex)
    {
        m_etags.push_back("");
    }

    m_etags[partIndex] = etag;
    aws_mutex_unlock(&m_etagsMutex);
}

bool MultipartTransferState::GetPartRangeForTransfer(
    uint32_t desiredNumParts,
    uint32_t &outStartPartIndex,
    uint32_t &outNumParts)
{
    if (IsCompleted())
    {
        return false;
    }

    uint32_t startPartIndex = m_numPartsRequested.fetch_add(desiredNumParts);

    if (startPartIndex >= m_numParts)
    {
        m_numPartsRequested = m_numParts;
        return false;
    }

    uint32_t numPartsToTransfer = desiredNumParts;

    if ((startPartIndex + numPartsToTransfer) >= m_numParts)
    {
        numPartsToTransfer = m_numParts - startPartIndex;
    }

    outStartPartIndex = startPartIndex;
    outNumParts = numPartsToTransfer;
    return true;
}

bool MultipartTransferState::IncNumPartsCompleted()
{
    uint32_t originalValue = m_numPartsCompleted.fetch_add(1);
    return (originalValue + 1) == GetNumParts();
}

const Aws::Crt::String &MultipartTransferState::GetKey() const
{
    return m_key;
}

void MultipartTransferState::GetETags(Aws::Crt::Vector<Aws::Crt::String> &outETags)
{
    aws_mutex_lock(&m_etagsMutex);
    outETags = m_etags;
    aws_mutex_unlock(&m_etagsMutex);
}

uint32_t MultipartTransferState::GetNumParts() const
{
    return m_numParts;
}

uint32_t MultipartTransferState::GetNumPartsRequested() const
{
    return m_numPartsRequested;
}

uint32_t MultipartTransferState::GetNumPartsCompleted() const
{
    return m_numPartsCompleted;
}

uint64_t MultipartTransferState::GetObjectSize() const
{
    return m_objectSize;
}
