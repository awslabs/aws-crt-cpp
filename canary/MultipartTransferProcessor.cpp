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

#include "MultipartTransferProcessor.h"
#include "S3ObjectTransport.h"
#include <aws/crt/Types.h>
#include <aws/crt/io/Stream.h>
#include <aws/io/stream.h>

#if defined(_WIN32)
#    undef min
#endif

using namespace Aws::Crt;

MultipartTransferProcessor::MultipartTransferProcessor(std::uint32_t streamsAvailable)
{
    m_streamsAvailable = streamsAvailable;

    aws_mutex_init(&m_queueMutex);
}

MultipartTransferProcessor::~MultipartTransferProcessor()
{
    aws_mutex_clean_up(&m_queueMutex);
}

void MultipartTransferProcessor::ProcessNextParts(uint32_t streamsReturning)
{
    ProcessNextPartsForNextObject(streamsReturning);

    while (ProcessNextPartsForNextObject(0))
    {
    }
}

bool MultipartTransferProcessor::ProcessNextPartsForNextObject(uint32_t streamsReturning)
{
    uint32_t startPartIndex = 0;
    uint32_t numPartsToProcess = 0;
    std::shared_ptr<MultipartTransferState> state;

    // Grab all of the streams currently available.
    uint32_t numStreamsToConsume = m_streamsAvailable.exchange(0);

    // Add the number of streams that we just got back to our local count.  If
    // we don't use it, then it'll get put it back into m_streamsAvailable.
    // This way, we guarantee that those streams get to be used locally, and do not
    // get stolen by another thread.
    numStreamsToConsume += streamsReturning;

    // If we have streams to consume, try to find something to use them.  We're not intending
    // to use all of them all of the time--we're just trying to find the next valid range of
    // parts to process.  Any remaining streams we grabbed will get thrown back into the pool
    // and will be handled with a different call to this method.
    if (numStreamsToConsume > 0)
    {
        bool processingQueue = true;

        // Find the next thing in the queue that needs parts processed, cleaning up the queue along the way.
        while (processingQueue)
        {
            state = PeekQueue();

            // If we have no state returned, the queue is empty, so stop processing.
            if (state == nullptr)
            {
                processingQueue = false;
            }
            // Try getting a range of parts to process.
            else if (state->GetPartRangeForTransfer(numStreamsToConsume, startPartIndex, numPartsToProcess))
            {
                numStreamsToConsume -= numPartsToProcess;
                AWS_FATAL_ASSERT(numStreamsToConsume >= 0);
                processingQueue = false;
            }
            // If GetPartRangeForTransfer returned false, then there's nothing left to process on that object,
            // so remove it from the queue.
            else
            {
                PopQueue(state);
            }
        }
    }

    // Put back the count of streams that we didn't use.
    m_streamsAvailable += numStreamsToConsume;

    // If we didn't get a state back, then numPartsToProcess should be zero.
    AWS_FATAL_ASSERT(state != nullptr || numPartsToProcess == 0);

    for (uint32_t i = 0; i < numPartsToProcess; ++i)
    {
        uint32_t partIndex = startPartIndex + i;
        uint32_t partNumber = partIndex + 1;

        uint64_t partByteStart = partIndex * S3ObjectTransport::MaxPartSizeBytes;
        uint64_t partByteRemainder = state->GetObjectSize() - (partIndex * S3ObjectTransport::MaxPartSizeBytes);
        uint64_t partByteSize = std::min(partByteRemainder, S3ObjectTransport::MaxPartSizeBytes);

        MultipartTransferState::PartInfo partInfo(partIndex, partNumber, partByteStart, partByteSize);

        state->ProcessPart(state, partInfo, [this]() { ProcessNextParts(1); });
    }

    return numPartsToProcess > 0;
}

void MultipartTransferProcessor::PushQueue(const std::shared_ptr<MultipartTransferState> &state)
{
    aws_mutex_lock(&m_queueMutex);
    m_stateQueue.push(state);
    aws_mutex_unlock(&m_queueMutex);

    ProcessNextParts(0);
}

std::shared_ptr<MultipartTransferState> MultipartTransferProcessor::PeekQueue()
{
    std::shared_ptr<MultipartTransferState> state;

    aws_mutex_lock(&m_queueMutex);

    if (m_stateQueue.size() > 0)
    {
        state = m_stateQueue.front();
    }

    aws_mutex_unlock(&m_queueMutex);

    return state;
}

void MultipartTransferProcessor::PopQueue(const std::shared_ptr<MultipartTransferState> &state)
{
    aws_mutex_lock(&m_queueMutex);
    if (m_stateQueue.size() > 0)
    {
        std::shared_ptr<MultipartTransferState> front = m_stateQueue.front();

        if (state == front)
        {
            m_stateQueue.pop();
        }
    }
    aws_mutex_unlock(&m_queueMutex);
}
