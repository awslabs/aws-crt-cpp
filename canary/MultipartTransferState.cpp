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
#include "CanaryApp.h"
#include "MetricsPublisher.h"
#include "S3ObjectTransport.h"

#include <aws/common/clock.h>
#include <aws/common/date_time.h>
#include <aws/crt/Api.h>
#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/Stream.h>
#include <aws/io/stream.h>
#include <cinttypes>

using namespace Aws::Crt;

MultipartTransferState::MultipartTransferState(const Aws::Crt::String &key, uint64_t objectSize, uint32_t numParts)
{
    m_isFinished = false;
    m_errorCode = AWS_ERROR_SUCCESS;
    m_numParts = numParts;
    m_numPartsCompleted = 0;
    m_objectSize = objectSize;
    m_key = key;
}

MultipartTransferState::~MultipartTransferState() {}

void MultipartTransferState::SetProcessPartCallback(const ProcessPartCallback &processPartCallback)
{
    m_processPartCallback = processPartCallback;
}

void MultipartTransferState::SetFinishedCallback(const FinishedCallback &finishedCallback)
{
    m_finishedCallback = finishedCallback;
}

void MultipartTransferState::SetFinished(int32_t errorCode)
{
    bool wasCompleted = m_isFinished.exchange(true);

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
        m_finishedCallback(m_errorCode);
    }
}

bool MultipartTransferState::IsFinished() const
{
    return m_isFinished;
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

uint32_t MultipartTransferState::GetNumParts() const
{
    return m_numParts;
}

uint32_t MultipartTransferState::GetNumPartsCompleted() const
{
    return m_numPartsCompleted;
}

uint64_t MultipartTransferState::GetObjectSize() const
{
    return m_objectSize;
}

MultipartUploadState::MultipartUploadState(const Aws::Crt::String &key, uint64_t objectSize, uint32_t numParts)
    : MultipartTransferState(key, objectSize, numParts)
{
    m_etags.reserve(numParts);

    for (uint32_t i = 0; i < numParts; ++i)
    {
        m_etags.push_back("");
    }
}

void MultipartUploadState::SetUploadId(const Aws::Crt::String &uploadId)
{
    m_uploadId = uploadId;
}

void MultipartUploadState::SetETag(uint32_t partIndex, const Aws::Crt::String &etag)
{
    std::lock_guard<std::mutex> lock(m_etagsMutex);

    AWS_FATAL_ASSERT(partIndex < m_etags.size());

    m_etags[partIndex] = etag;
}

const Aws::Crt::String &MultipartUploadState::GetUploadId() const
{
    return m_uploadId;
}

void MultipartUploadState::GetETags(Aws::Crt::Vector<Aws::Crt::String> &outETags)
{
    std::lock_guard<std::mutex> lock(m_etagsMutex);
    outETags = m_etags;
}

MultipartDownloadState::MultipartDownloadState(const Aws::Crt::String &key, uint64_t objectSize, uint32_t numParts)
    : MultipartTransferState(key, objectSize, numParts)
{
}
