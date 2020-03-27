#include "MeasureTransferRateStream.h"

using namespace Aws::Crt;

namespace
{
    size_t BodyTemplateSize = 4ULL * 1024ULL;
    thread_local char *BodyTemplate = nullptr;
} // namespace

bool MeasureTransferRateStream::IsValid() const noexcept
{
    return true;
}

bool MeasureTransferRateStream::ReadImpl(ByteBuf &dest) noexcept
{
    if (BodyTemplate == nullptr)
    {
        char BodyTemplateData[] =
            "This is a test string for use with canary testing against Amazon Simple Storage Service";

        BodyTemplate = new char[BodyTemplateSize];
        BodyTemplate[BodyTemplateSize - 1] = '\0';

        size_t totalToWrite = BodyTemplateSize;
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

    // A quick way for us to measure how much data we've actually written to S3 storage.  (This working is reliant
    // on this function only being used when we are reading data from the stream while sending that data to S3.)
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
    : m_canaryApp(canaryApp), m_transferState(transferState), m_allocator(allocator), m_written(0)
{
    (void)m_canaryApp;
    (void)m_allocator;
}