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

#include "MeasureTransferRateStream.h"
#include <algorithm>

#ifdef WIN32
#    undef min
#    undef max
#endif

using namespace Aws::Crt;

namespace
{
    const uint64_t BodyTemplateSize = 4ULL * 1024ULL;
    thread_local char BodyTemplate[BodyTemplateSize] = "";
} // namespace

MeasureTransferRateStream::MeasureTransferRateStream(
    CanaryApp &canaryApp,
    const std::shared_ptr<TransferState> &transferState,
    uint64_t length)
    : InputStream(g_allocator), m_canaryApp(canaryApp), m_transferState(transferState), m_length(length), m_written(0)
{
    (void)m_canaryApp;
}

bool MeasureTransferRateStream::IsValid() const noexcept
{
    return true;
}

bool MeasureTransferRateStream::ReadImpl(ByteBuf &dest) noexcept
{
    if (BodyTemplate[0] == '\0')
    {
        char BodyTemplateData[] =
            "This is a test string for use with canary testing against Amazon Simple Storage Service";

        BodyTemplate[BodyTemplateSize - 1] = '\0';

        size_t totalToWrite = BodyTemplateSize - 1;
        char *BodyTemplatePos = BodyTemplate;

        while (totalToWrite)
        {
            size_t toWrite = AWS_ARRAY_SIZE(BodyTemplateData) - 1 > totalToWrite ? totalToWrite
                                                                                 : AWS_ARRAY_SIZE(BodyTemplateData) - 1;

            memcpy(BodyTemplatePos, BodyTemplateData, toWrite);

            BodyTemplatePos += toWrite;
            totalToWrite -= toWrite;
        }
    }

    AWS_FATAL_ASSERT(m_written <= m_length);

    uint64_t totalBufferSpace = dest.capacity - dest.len;
    uint64_t unwritten = m_length - m_written;

    uint64_t amountToWrite = std::min(totalBufferSpace, unwritten);
    uint64_t writtenOut = 0;

    while (amountToWrite > 0)
    {
        uint64_t toWrite = std::min((BodyTemplateSize - 1), amountToWrite);
        ByteCursor outCur = ByteCursorFromArray((const uint8_t *)BodyTemplate, toWrite);

        aws_byte_buf_append(&dest, &outCur);

        writtenOut += toWrite;
        amountToWrite = amountToWrite - toWrite;
    }

    m_written += writtenOut;

    if (!m_transferState->HasDataUpMetrics())
    {
        m_transferState->InitDataUpMetric();
    }

    m_transferState->ConsumeQueuedDataUpMetric();
    m_transferState->QueueDataUpMetric(writtenOut);

    return true;
}

Io::StreamStatus MeasureTransferRateStream::GetStatusImpl() const noexcept
{
    Io::StreamStatus status;
    status.is_end_of_stream = m_written == m_length;
    status.is_valid = !status.is_end_of_stream;

    return status;
}

bool MeasureTransferRateStream::SeekImpl(Io::OffsetType, Io::StreamSeekBasis) noexcept
{
    m_written = 0;
    return true;
}

int64_t MeasureTransferRateStream::GetLengthImpl() const noexcept
{
    return m_length;
}
