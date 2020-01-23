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
#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/io/Stream.h>
#include <aws/io/stream.h>

#if defined(_WIN32)
#    undef min
#endif

using namespace Aws::Crt;

const uint32_t MultipartTransferProcessor::NumPartsPerTask = 2048;

MultipartTransferProcessor::ProcessPartRangeTaskArgs::ProcessPartRangeTaskArgs(
    MultipartTransferProcessor &inTransferProcessor,
    uint32_t inPartRangeStart,
    uint32_t inPartRangeLength,
    const std::shared_ptr<std::vector<QueuedPart>> &inParts)
    : transferProcessor(inTransferProcessor), partRangeStart(inPartRangeStart), partRangeLength(inPartRangeLength),
      parts(inParts)
{
}

MultipartTransferProcessor::MultipartTransferProcessor(
    Aws::Crt::Io::EventLoopGroup &elGroup,
    std::uint32_t streamsAvailable)
{
    m_streamsAvailable = streamsAvailable;
    m_schedulingLoop = aws_event_loop_group_get_next_loop(elGroup.GetUnderlyingHandle());
}

void MultipartTransferProcessor::ProcessNextParts(uint32_t streamsReturning)
{
    std::shared_ptr<MultipartTransferState> state;
    std::vector<QueuedPart> parts;
    std::shared_ptr<std::vector<QueuedPart>> partsShared;

    // Grab all of the streams available in the shared pool and add our own number of streams
    // that we know locally can be returned.  If we don't end up needing that stream (or
    // any of the streams we've just nabbed from m_streamsAvailable) it will be added back
    // to m_streamsAvailable soon.  By not adding streamsReturning to m_streams Available
    // right away, we guarantee that locally we can use the amount in "streamsReturning",
    // and that it won't be grabbed by another thread.
    uint32_t numStreamsToConsume = m_streamsAvailable.exchange(0) + streamsReturning;

    // Grab all of the parts that we can consume and put them into our local parts vector.
    uint32_t numPartsToProcess = PopQueue(numStreamsToConsume, parts);

    // Figure out how many tasks this should be distributed among.  If only 1, we'll
    // do the processing from the current thread.
    uint32_t numTasksNeeded = numPartsToProcess / NumPartsPerTask;

    m_streamsAvailable += (numStreamsToConsume - numPartsToProcess);

    if ((numPartsToProcess % NumPartsPerTask) > 0)
    {
        ++numTasksNeeded;
    }

    if (numTasksNeeded == 0)
    {
        return;
    }
    else if (numTasksNeeded == 1)
    {
        ProcessPartRange(parts, 0, numPartsToProcess);
    }
    else if (numTasksNeeded > 1)
    {
        // Multiple tasks will be reading from the queue, so move it into a shared pointer
        // that will auto-cleanup once the tasks referencing it go away.
        partsShared = std::make_shared<std::vector<QueuedPart>>();
        *partsShared = std::move(parts);

        uint32_t numParts = static_cast<uint32_t>(partsShared->size());

        // Setup and create each task needed
        for (uint32_t i = 0; i < numTasksNeeded; ++i)
        {
            uint32_t partRangeStart = i * NumPartsPerTask;
            uint32_t partRangeLength = NumPartsPerTask;

            if ((partRangeStart + partRangeLength) > numParts)
            {
                partRangeLength = numParts - partRangeStart;
            }

            ProcessPartRangeTaskArgs *args =
                New<ProcessPartRangeTaskArgs>(g_allocator, *this, partRangeStart, partRangeLength, partsShared);

            aws_task processPartRangeTask;
            AWS_ZERO_STRUCT(processPartRangeTask);

            aws_task_init(
                &processPartRangeTask,
                MultipartTransferProcessor::s_ProcessPartRangeTask,
                reinterpret_cast<void *>(args),
                nullptr);

            {
                std::lock_guard<std::mutex> lock(m_schedulingMutex);
                aws_event_loop_schedule_task_now(m_schedulingLoop, &processPartRangeTask);
            }
        }
    }
}

void MultipartTransferProcessor::s_ProcessPartRangeTask(aws_task *task, void *arg, aws_task_status status)
{
    (void)task;

    if (status != AWS_TASK_STATUS_RUN_READY)
    {
        return;
    }

    ProcessPartRangeTaskArgs *args = reinterpret_cast<ProcessPartRangeTaskArgs *>(arg);
    args->transferProcessor.ProcessPartRange(*args->parts, args->partRangeStart, args->partRangeLength);

    Delete(args, g_allocator);
    args = nullptr;
}

void MultipartTransferProcessor::ProcessPartRange(
    const std::vector<QueuedPart> &parts,
    uint32_t rangeStart,
    uint32_t rangeLength)
{
    uint32_t rangeEnd = rangeStart + rangeLength;
    uint32_t numSkipped = 0;

    for (uint32_t i = rangeStart; i < rangeEnd; ++i)
    {
        std::shared_ptr<MultipartTransferState> state = parts[i].state;

        if (state->IsFinished())
        {
            ++numSkipped;
            continue;
        }

        uint32_t partIndex = parts[i].partIndex;
        uint32_t partNumber = partIndex + 1;

        uint64_t partByteStart = partIndex * S3ObjectTransport::MaxPartSizeBytes;
        uint64_t partByteRemainder = state->GetObjectSize() - (partIndex * S3ObjectTransport::MaxPartSizeBytes);
        uint64_t partByteSize = std::min(partByteRemainder, S3ObjectTransport::MaxPartSizeBytes);

        MultipartTransferState::PartInfo partInfo(partIndex, partNumber, partByteStart, partByteSize);

        state->ProcessPart(partInfo, [this]() { ProcessNextParts(1); });
    }

    if (numSkipped > 0)
    {
        ProcessNextParts(numSkipped);
    }
}

void MultipartTransferProcessor::PushQueue(const std::shared_ptr<MultipartTransferState> &state)
{
    {
        std::lock_guard<std::mutex> lock(m_partQueueMutex);

        for (uint32_t i = 0; i < state->GetNumParts(); ++i)
        {
            QueuedPart queuedPart = {state, i};
            m_partQueue.push(queuedPart);
        }
    }

    ProcessNextParts(0);
}

uint32_t MultipartTransferProcessor::PopQueue(uint32_t numRequested, std::vector<QueuedPart> &parts)
{
    uint32_t numAdded = 0;
    std::lock_guard<std::mutex> lock(m_partQueueMutex);

    while (m_partQueue.size() > 0 && numAdded < numRequested)
    {
        const QueuedPart &front = m_partQueue.front();

        if (!front.state->IsFinished())
        {
            parts.push_back(front);
            ++numAdded;
        }

        m_partQueue.pop();
    }

    return numAdded;
}
