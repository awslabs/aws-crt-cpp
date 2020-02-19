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
#include "CanaryApp.h"
#include "S3ObjectTransport.h"

#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/io/Stream.h>
#include <aws/io/stream.h>

#include <aws/common/clock.h>
#include <aws/common/system_info.h>

#if defined(_WIN32)
#    undef min
#endif

using namespace Aws::Crt;

const uint32_t MultipartTransferProcessor::NumPartsPerTask = 100;

MultipartTransferProcessor::ProcessPartRangeTaskArgs::ProcessPartRangeTaskArgs(
    MultipartTransferProcessor &inTransferProcessor,
    uint32_t inPartRangeStart,
    uint32_t inPartRangeLength,
    const std::shared_ptr<Vector<QueuedPart>> &inParts)
    : transferProcessor(inTransferProcessor), partRangeStart(inPartRangeStart), partRangeLength(inPartRangeLength),
      parts(inParts)
{
}

MultipartTransferProcessor::MultipartTransferProcessor(
    CanaryApp &canaryApp,
    Aws::Crt::Io::EventLoopGroup &elGroup,
    std::uint32_t streamsAvailable)
    : m_canaryApp(canaryApp)
{
    m_streamsAvailable = streamsAvailable;
    m_schedulingLoop = aws_event_loop_group_get_next_loop(elGroup.GetUnderlyingHandle());

    ScheduleLogOutputTask();
}

void MultipartTransferProcessor::ProcessNextParts(uint32_t streamsReturning)
{
    std::shared_ptr<MultipartTransferState> state;
    std::shared_ptr<Vector<QueuedPart>> parts = MakeShared<Vector<QueuedPart>>(g_allocator);

    // Grab all of the streams available in the shared pool and add our own number of streams
    // that we know locally can be returned.  If we don't end up needing that stream (or
    // any of the streams we've just nabbed from m_streamsAvailable) it will be added back
    // to m_streamsAvailable soon.  By not adding streamsReturning to m_streams Available
    // right away, we guarantee that locally we can use the amount in "streamsReturning",
    // and that it won't be grabbed by another thread.
    uint32_t numStreamsToConsume = m_streamsAvailable.exchange(0) + streamsReturning;

    // Grab all of the parts that we can consume and put them into our local parts vector.
    uint32_t numPartsToProcess = PopQueue(numStreamsToConsume, *parts);

    // Figure out how many tasks this should be distributed among.
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

    uint32_t numParts = static_cast<uint32_t>(parts->size());

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
            New<ProcessPartRangeTaskArgs>(g_allocator, *this, partRangeStart, partRangeLength, parts);

        aws_task *processPartRangeTask = New<aws_task>(g_allocator);
        aws_task_init(
            processPartRangeTask,
            MultipartTransferProcessor::s_ProcessPartRangeTask,
            reinterpret_cast<void *>(args),
            "ProcessPartRangeTask");

        aws_event_loop_schedule_task_now(m_schedulingLoop, processPartRangeTask);
    }
}

void MultipartTransferProcessor::s_ProcessPartRangeTask(aws_task *task, void *argsVoid, aws_task_status status)
{
    if (status != AWS_TASK_STATUS_RUN_READY)
    {
        return;
    }

    ProcessPartRangeTaskArgs *args = reinterpret_cast<ProcessPartRangeTaskArgs *>(argsVoid);
    args->transferProcessor.ProcessPartRange(*args->parts, args->partRangeStart, args->partRangeLength);

    Delete(task, g_allocator);
    task = nullptr;

    Delete(args, g_allocator);
    args = nullptr;
}

void MultipartTransferProcessor::ProcessPartRange(
    const Vector<QueuedPart> &parts,
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

        uint64_t partByteInterval = state->GetObjectSize() / static_cast<uint64_t>(state->GetNumParts());
        uint64_t partByteStart = partIndex * partByteInterval;
        uint64_t partByteSize = partByteInterval;

        if (partNumber == state->GetNumParts())
        {
            partByteSize += state->GetObjectSize() % static_cast<uint64_t>(state->GetNumParts());
        }

        std::shared_ptr<MultipartTransferState::PartInfo> partInfo = MakeShared<MultipartTransferState::PartInfo>(
            g_allocator, m_canaryApp.publisher, partIndex, partNumber, partByteStart, partByteSize);

        // TODO should state and partInfo be captured as weak pointers here?
        state->ProcessPart(partInfo, [this, state, partInfo](PartFinishResponse response) {
            if (response == PartFinishResponse::Done)
            {
                ProcessNextParts(1);
            }
            else if (response == PartFinishResponse::Retry)
            {
                RepushQueue(state, partInfo->partIndex);
            }
            else
            {
                AWS_FATAL_ASSERT(false);
            }
        });
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

void MultipartTransferProcessor::RepushQueue(const std::shared_ptr<MultipartTransferState> &state, uint32_t partIndex)
{
    {
        std::lock_guard<std::mutex> lock(m_partQueueMutex);

        QueuedPart queuedPart = {state, partIndex};
        m_partQueue.push(queuedPart);
    }

    ProcessNextParts(1);
}

uint32_t MultipartTransferProcessor::PopQueue(uint32_t numRequested, Vector<QueuedPart> &parts)
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

// TODO either remove entirely or move somewhere else.  This should be in a more general place and is responsible
// for more than logging now.
void MultipartTransferProcessor::ScheduleLogOutputTask()
{
    aws_task *logOutputTask = New<aws_task>(g_allocator);
    aws_task_init(
        logOutputTask, MultipartTransferProcessor::s_LogOutputTask, reinterpret_cast<void *>(this), "LogOutputTask");

    const uint64_t frequency = aws_timestamp_convert(5000, AWS_TIMESTAMP_MILLIS, AWS_TIMESTAMP_NANOS, NULL);

    uint64_t now = 0;
    aws_event_loop_current_clock_time(m_schedulingLoop, &now);
    aws_event_loop_schedule_task_future(m_schedulingLoop, logOutputTask, now + frequency);
}

void MultipartTransferProcessor::s_LogOutputTask(aws_task *task, void *arg, aws_task_status status)
{
    if (status != AWS_TASK_STATUS_RUN_READY)
    {
        return;
    }

    Delete(task, g_allocator);
    task = nullptr;

    MultipartTransferProcessor *processor = reinterpret_cast<MultipartTransferProcessor *>(arg);
    std::shared_ptr<S3ObjectTransport> transport = processor->m_canaryApp.transport;

    uint32_t queueSize = 0;

    {
        std::lock_guard<std::mutex> lock(processor->m_partQueueMutex);
        queueSize = (uint32_t)processor->m_partQueue.size();
    }

    size_t openConnectionCount = transport->GetOpenConnectionCount();

    Metric connMetric;
    connMetric.Unit = MetricUnit::Count;
    connMetric.Value = (double)openConnectionCount;
    connMetric.SetTimestampNow();
    connMetric.MetricName = "NumConnections";
    processor->m_canaryApp.publisher->AddDataPoint(connMetric);

	const Aws::Crt::String &s3Endpoint = processor->m_canaryApp.transport->GetEndPoint();
    size_t s3AddressCount = processor->m_canaryApp.defaultHostResolver.GetHostAddressCount(s3Endpoint);

    Metric s3AddressCountMetric;
    s3AddressCountMetric.Unit = MetricUnit::Count;
    s3AddressCountMetric.Value = (double)s3AddressCount;
    s3AddressCountMetric.SetTimestampNow();
    s3AddressCountMetric.MetricName = "S3AddressCount";
    processor->m_canaryApp.publisher->AddDataPoint(s3AddressCountMetric);

    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY,
        "Streams-available:%d  Number-of-parts-in-queue:%d  Open-connections:%d  Number-of-s3-addresses: %d",
        processor->m_streamsAvailable.load(),

        queueSize,
        (uint32_t)openConnectionCount,
        (uint32_t)s3AddressCount);

    processor->ScheduleLogOutputTask();
}
