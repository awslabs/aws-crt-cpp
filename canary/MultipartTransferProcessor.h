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

#include <memory>
#include <queue>

#include <aws/common/mutex.h>

#include "MultipartTransferState.h"

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

class MultipartTransferProcessor
{
  public:
    MultipartTransferProcessor(Aws::Crt::Io::EventLoopGroup &elGroup, std::uint32_t streamsAvailable);

    void PushQueue(const std::shared_ptr<MultipartTransferState> &uploadState);

  private:
    static const uint32_t NumPartsPerTask;

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
        std::shared_ptr<std::vector<QueuedPart>> parts;

        ProcessPartRangeTaskArgs(
            MultipartTransferProcessor &transferProcessor,
            uint32_t partRangeStart,
            uint32_t partRangeLength,
            const std::shared_ptr<std::vector<QueuedPart>> &parts);
    };

    aws_event_loop *m_schedulingLoop;
    std::atomic<uint32_t> m_streamsAvailable;
    std::mutex m_partQueueMutex;
    std::mutex m_schedulingMutex;
    std::queue<QueuedPart> m_partQueue;

    static void s_ProcessPartRangeTask(aws_task *task, void *arg, aws_task_status status);

    void ProcessNextParts(uint32_t streamsReturning);
    void ProcessPartRange(const std::vector<QueuedPart> &parts, uint32_t partRangeStart, uint32_t partRangeLength);
    uint32_t PopQueue(uint32_t numRequested, std::vector<QueuedPart> &parts);
};
