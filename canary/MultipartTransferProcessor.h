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

#include <memory>
#include <queue>

#include "MultipartTransferState.h"
#include <aws/common/mutex.h>
#include <aws/crt/Types.h>

class CanaryApp;
class S3ObjectTransport;

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            class EventLoopGroup;
        }
    } // namespace Crt
} // namespace Aws

/*
 * Processes parts of each multipart transfer state passed in via PushQueue, allowing individual parts
 * to be re-pushed if needed in the event of failure.
 */
class MultipartTransferProcessor
{
  public:
    MultipartTransferProcessor(
        CanaryApp &canaryApp,
        Aws::Crt::Io::EventLoopGroup &elGroup,
        std::uint32_t streamsAvailable);

    /*
     * Push a multipart transfer state for processing.
     */
    void PushQueue(const std::shared_ptr<MultipartTransferState> &uploadState);

    /*
     * Re-push an individual part of a multipart transfer state for processing.
     */
    void RepushQueue(const std::shared_ptr<MultipartTransferState> &state, uint32_t partIndex);

  private:
    struct QueuedPart
    {
        std::shared_ptr<MultipartTransferState> state;
        uint32_t partIndex;
    };

    struct ProcessPartRangeTaskArgs
    {
        aws_task task;
        MultipartTransferProcessor &transferProcessor;
        uint32_t partRangeStart;
        uint32_t partRangeLength;
        std::shared_ptr<Aws::Crt::Vector<QueuedPart>> parts;

        ProcessPartRangeTaskArgs(
            MultipartTransferProcessor &transferProcessor,
            uint32_t partRangeStart,
            uint32_t partRangeLength,
            const std::shared_ptr<Aws::Crt::Vector<QueuedPart>> &parts);
    };

    CanaryApp &m_canaryApp;
    aws_event_loop *m_schedulingLoop;
    std::atomic<uint32_t> m_streamsAvailable;
    std::mutex m_partQueueMutex;
    std::mutex m_schedulingMutex;
    std::queue<QueuedPart> m_partQueue;

    static void s_ProcessPartRangeTask(aws_task *task, void *arg, aws_task_status status);

    void ProcessNextParts(uint32_t streamsReturning);
    void ProcessPartRange(const Aws::Crt::Vector<QueuedPart> &parts, uint32_t partRangeStart, uint32_t partRangeLength);
    uint32_t PopQueue(uint32_t numRequested, Aws::Crt::Vector<QueuedPart> &parts);
};
