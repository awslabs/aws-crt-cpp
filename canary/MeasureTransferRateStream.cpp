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

using namespace Aws::Crt;

namespace
{
    const size_t BodyTemplateSize = 4ULL * 1024ULL;
    thread_local char BodyTemplate[BodyTemplateSize] = "";
} // namespace

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

    size_t totalBufferSpace = dest.capacity - dest.len;
    size_t unwritten = m_transferState->GetSizeInBytes() - m_written;
    size_t totalToWrite = totalBufferSpace > unwritten ? unwritten : totalBufferSpace;
    size_t writtenOut = 0;

    while (totalToWrite)
    {
        size_t toWrite = BodyTemplateSize - 1 > totalToWrite ? totalToWrite : BodyTemplateSize - 1;
        ByteCursor outCur = ByteCursorFromArray((const uint8_t *)BodyTemplate, toWrite);

        aws_byte_buf_append(&dest, &outCur);

        writtenOut += toWrite;
        totalToWrite = totalToWrite - toWrite;
    }

    m_written += writtenOut;

    m_transferState->AddDataUpMetric(writtenOut);

    return true;
}

Io::StreamStatus MeasureTransferRateStream::GetStatusImpl() const noexcept
{
    Io::StreamStatus status;
    status.is_end_of_stream = m_written == m_transferState->GetSizeInBytes();
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
    return m_transferState->GetSizeInBytes();
}

MeasureTransferRateStream::MeasureTransferRateStream(
    CanaryApp &canaryApp,
    const std::shared_ptr<TransferState> &transferState,
    Allocator *allocator)
    : InputStream(allocator), m_canaryApp(canaryApp), m_transferState(transferState), m_written(0)
{
    (void)m_canaryApp;
}