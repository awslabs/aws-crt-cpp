#include "MultipartTransferState.h"
#include "S3ObjectTransport.h"

#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/stream.h>
#include <aws/io/stream.h>

using namespace Aws::Crt;

MultipartTransferState::MultipartTransferState(
    const Aws::Crt::String &key,
    const Aws::Crt::String &uploadId,
    uint64_t objectSize,
    uint32_t numParts,
    GetObjectPartCallback getObjectPartCallback,
    OnCompletedCallback onCompletedCallback)
{
    m_isCompleted = false;
    m_errorCode = AWS_OP_SUCCESS;
    m_numParts = numParts;
    m_numPartsRequested = 0;
    m_numPartsCompleted = 0;
    m_objectSize = objectSize;
    m_key = key;
    m_uploadId = uploadId;
    m_getObjectPartCallback = getObjectPartCallback;
    m_onCompletedCallback = onCompletedCallback;

    for (size_t i = 0; i < m_numParts; ++i)
    {
        m_etags.push_back("");
    }

    aws_mutex_init(&m_completionMutex);
    aws_mutex_init(&m_etagsMutex);
}

MultipartTransferState::~MultipartTransferState()
{
    aws_mutex_clean_up(&m_completionMutex);
    aws_mutex_clean_up(&m_etagsMutex);
}

void MultipartTransferState::SetCompleted(int32_t errorCode)
{
    aws_mutex_lock(&m_completionMutex);

    if (m_isCompleted)
    {
        AWS_LOGF_INFO(
            AWS_LS_COMMON_GENERAL,
            "MultipartTransferState::SetCompleted being called multiple times--not recording error code %d.",
            errorCode);
    }
    else
    {
        m_isCompleted = true;
        m_errorCode = errorCode;
        m_onCompletedCallback(m_errorCode);
    }

    aws_mutex_unlock(&m_completionMutex);
}

bool MultipartTransferState::IsCompleted() const
{
    return m_isCompleted;
}

void MultipartTransferState::SetETag(uint32_t partIndex, const Aws::Crt::String &etag)
{
    aws_mutex_lock(&m_etagsMutex);
    m_etags[partIndex] = etag;
    aws_mutex_unlock(&m_etagsMutex);
}

bool MultipartTransferState::GetPartsForUpload(
    uint32_t desiredNumParts,
    uint32_t &outStartPartIndex,
    uint32_t &outNumParts)
{
    uint32_t startPartIndex = m_numPartsRequested.fetch_add(desiredNumParts);
    uint32_t numPartsToUpload = 0;

    if (startPartIndex >= m_numParts)
    {
        m_numPartsRequested = m_numParts;
        return false;
    }

    if ((startPartIndex + desiredNumParts) >= m_numParts)
    {
        numPartsToUpload = m_numParts - startPartIndex;
    }

    outStartPartIndex = startPartIndex;
    outNumParts = numPartsToUpload;
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

const Aws::Crt::String &MultipartTransferState::GetUploadId() const
{
    return m_uploadId;
}

void MultipartTransferState::GetETags(Aws::Crt::Vector<Aws::Crt::String> & outETags)
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
