#include "MeasureTransferRate.h"
#include "CanaryUtil.h"
#include "MetricsPublisher.h"
#include "S3ObjectTransport.h"
#include <aws/common/system_info.h>

using namespace Aws::Crt;

const char MeasureTransferRate::BodyTemplate[] =
    "This is a test string for use with canary testing against Amazon Simple Storage Service";

const size_t MeasureTransferRate::SmallObjectSize = 16 * 1024 * 1024;
const size_t MeasureTransferRate::LargeObjectSize = 10 * S3ObjectTransport::MaxPartSizeBytes;

aws_input_stream_vtable MeasureTransferRate::s_templateStreamVTable = {s_templateStreamSeek,
                                                                       s_templateStreamRead,
                                                                       s_templateStreamGetStatus,
                                                                       s_templateStreamGetLength,
                                                                       s_templateStreamDestroy};

int MeasureTransferRate::s_templateStreamRead(struct aws_input_stream *stream, struct aws_byte_buf *dest)
{
    auto templateStream = static_cast<TemplateStream *>(stream->impl);

    size_t totalBufferSpace = dest->capacity - dest->len;
    size_t unwritten = templateStream->length - templateStream->written;
    size_t totalToWrite = totalBufferSpace > unwritten ? unwritten : totalBufferSpace;
    size_t writtenOut = 0;

    while (totalToWrite)
    {
        size_t toWrite =
            AWS_ARRAY_SIZE(BodyTemplate) - 1 > totalToWrite ? totalToWrite : AWS_ARRAY_SIZE(BodyTemplate) - 1;
        ByteCursor outCur = ByteCursorFromArray((const uint8_t *)BodyTemplate, toWrite);

        aws_byte_buf_append(dest, &outCur);

        writtenOut += toWrite;
        totalToWrite = totalToWrite - toWrite;
    }

    templateStream->written += writtenOut;

    if (templateStream->length == templateStream->written)
    {
        Metric uploadMetric;
        uploadMetric.MetricName = "BytesUp";
        uploadMetric.Timestamp = DateTime::Now();
        uploadMetric.Value = (double)templateStream->length;
        uploadMetric.Unit = MetricUnit::Bytes;

        templateStream->publisher->AddDataPoint(uploadMetric);
    }

    return AWS_OP_SUCCESS;
}

int MeasureTransferRate::s_templateStreamGetStatus(struct aws_input_stream *stream, struct aws_stream_status *status)
{
    auto templateStream = static_cast<TemplateStream *>(stream->impl);

    status->is_end_of_stream = templateStream->written == templateStream->length;
    status->is_valid = !status->is_end_of_stream;

    return AWS_OP_SUCCESS;
}

int MeasureTransferRate::s_templateStreamSeek(
    struct aws_input_stream *stream,
    aws_off_t offset,
    enum aws_stream_seek_basis basis)
{
    (void)offset;
    (void)basis;

    auto templateStream = static_cast<TemplateStream *>(stream->impl);
    templateStream->written = 0;
    return AWS_OP_SUCCESS;
}

int MeasureTransferRate::s_templateStreamGetLength(struct aws_input_stream *stream, int64_t *length)
{
    auto templateStream = static_cast<TemplateStream *>(stream->impl);
    *length = templateStream->length;
    return AWS_OP_SUCCESS;
}

void MeasureTransferRate::s_templateStreamDestroy(struct aws_input_stream *stream)
{
    auto templateStream = static_cast<TemplateStream *>(stream->impl);
    Delete(templateStream, stream->allocator);
}

aws_input_stream *MeasureTransferRate::s_createTemplateStream(
    Allocator *alloc,
    MetricsPublisher *publisher,
    size_t length)
{
    auto templateStream = New<TemplateStream>(alloc);
    templateStream->publisher = publisher;
    templateStream->length = length;
    templateStream->written = 0;
    templateStream->inputStream.allocator = alloc;
    templateStream->inputStream.impl = templateStream;
    templateStream->inputStream.vtable = &s_templateStreamVTable;

    return &templateStream->inputStream;
}

void MeasureTransferRate::MeasureSmallObjectTransfer(
    Allocator *allocator,
    S3ObjectTransport &transport,
    MetricsPublisher &publisher,
    double cutOffTime)
{
    uint32_t threadCount = static_cast<uint32_t>(aws_system_info_processor_count());
    uint32_t maxInFlight = threadCount * 10;

    PerformMeasurement(
        allocator,
        transport,
        publisher,
        maxInFlight,
        SmallObjectSize,
        cutOffTime,
        MeasureTransferRate::s_TransferSmallObject);
}

void MeasureTransferRate::MeasureLargeObjectTransfer(
    Allocator *allocator,
    S3ObjectTransport &transport,
    MetricsPublisher &publisher,
    double cutOffTime)
{
    PerformMeasurement(
        allocator, transport, publisher, 2, LargeObjectSize, cutOffTime, MeasureTransferRate::s_TransferLargeObject);
}

template <typename TPeformTransferType>
void MeasureTransferRate::PerformMeasurement(
    Allocator *allocator,
    S3ObjectTransport &transport,
    MetricsPublisher &publisher,
    uint32_t maxConcurrentTransfers,
    uint64_t objectSize,
    double cutOffTime,
    const TPeformTransferType &&performTransfer)
{
    bool continueInitiatingTransfers = true;
    uint64_t counter = INT64_MAX;
    std::atomic<size_t> inFlightUploads(0);
    std::atomic<size_t> inFlightUploadOrDownload(0);

    time_t initialTime;
    time(&initialTime);

    while (continueInitiatingTransfers || inFlightUploadOrDownload > 0)
    {
        if (counter == 0)
        {
            counter = INT64_MAX;
        }

        while (continueInitiatingTransfers && inFlightUploads < maxConcurrentTransfers)
        {
            StringStream keyStream;
            keyStream << "crt-canary-obj-" << counter--;
            ++inFlightUploads;
            ++inFlightUploadOrDownload;
            auto key = keyStream.str();

            NotifyUploadFinished notifyUploadFinished =
                [&publisher, &inFlightUploads, &inFlightUploadOrDownload](int32_t errorCode) {
                    --inFlightUploads;

                    if (errorCode == AWS_ERROR_SUCCESS)
                    {
                        Metric successMetric;
                        successMetric.MetricName = "SuccessfulTransfer";
                        successMetric.Unit = MetricUnit::Count;
                        successMetric.Value = 1;
                        successMetric.Timestamp = DateTime::Now();

                        publisher.AddDataPoint(successMetric);
                    }
                    else
                    {
                        Metric failureMetric;
                        failureMetric.MetricName = "FailedTransfer";
                        failureMetric.Unit = MetricUnit::Count;
                        failureMetric.Value = 1;
                        failureMetric.Timestamp = DateTime::Now();

                        publisher.AddDataPoint(failureMetric);

                        --inFlightUploadOrDownload;
                    }
                };

            NotifyDownloadProgress notifyDownloadProgress = [&publisher](uint64_t dataLength) {
                std::shared_ptr<Metric> downMetric = MakeShared<Metric>(g_allocator);
                downMetric->MetricName = "BytesDown";
                downMetric->Unit = MetricUnit::Bytes;
                downMetric->Value = static_cast<double>(dataLength);
                downMetric->Timestamp = DateTime::Now();
                publisher.AddDataPoint(*downMetric);
            };

            NotifyDownloadFinished notifyDownloadFinished = [&publisher, &inFlightUploadOrDownload](int32_t errorCode) {
                if (errorCode == AWS_ERROR_SUCCESS)
                {
                    Metric successMetric;
                    successMetric.MetricName = "SuccessfulTransfer";
                    successMetric.Unit = MetricUnit::Count;
                    successMetric.Value = 1;
                    successMetric.Timestamp = DateTime::Now();

                    publisher.AddDataPoint(successMetric);
                }
                else
                {
                    Metric failureMetric;
                    failureMetric.MetricName = "FailedTransfer";
                    failureMetric.Unit = MetricUnit::Count;
                    failureMetric.Value = 1;
                    failureMetric.Timestamp = DateTime::Now();

                    publisher.AddDataPoint(failureMetric);
                }

                --inFlightUploadOrDownload;
            };

            performTransfer(
                allocator,
                transport,
                publisher,
                key,
                objectSize,
                notifyUploadFinished,
                notifyDownloadProgress,
                notifyDownloadFinished);
        }

        Metric memMetric;
        memMetric.Unit = MetricUnit::Bytes;
        memMetric.Value = (double)aws_mem_tracer_bytes(allocator);
        memMetric.Timestamp = DateTime::Now();
        memMetric.MetricName = "BytesAllocated";
        publisher.AddDataPoint(memMetric);
        std::this_thread::sleep_for(std::chrono::seconds(1));

        time_t currentTime;
        time(&currentTime);
        double elapsedSeconds = difftime(currentTime, initialTime);
        continueInitiatingTransfers = elapsedSeconds <= cutOffTime;
    }

    publisher.WaitForLastPublish();
}

void MeasureTransferRate::s_TransferSmallObject(
    Allocator *allocator,
    S3ObjectTransport &transport,
    MetricsPublisher &publisher,
    const String &key,
    uint64_t objectSize,
    const NotifyUploadFinished &notifyUploadFinished,
    const NotifyDownloadProgress &notifyDownloadProgress,
    const NotifyDownloadFinished &notifyDownloadFinished)
{
    transport.PutObject(
        key,
        s_createTemplateStream(allocator, &publisher, objectSize),
        [&transport, key, notifyUploadFinished, notifyDownloadProgress, notifyDownloadFinished](
            int32_t errorCode, std::shared_ptr<Aws::Crt::String>) {
            notifyUploadFinished(errorCode);

            if (errorCode != AWS_ERROR_SUCCESS)
            {
                notifyDownloadFinished(AWS_ERROR_UNKNOWN);
                return;
            }

            transport.GetObject(
                key,
                [notifyDownloadProgress](const Http::HttpStream &, const ByteCursor &cur) {
                    notifyDownloadProgress(cur.len);
                },
                [notifyDownloadFinished](int32_t errorCode) { notifyDownloadFinished(errorCode); });
        });
}

void MeasureTransferRate::s_TransferLargeObject(
    Allocator *allocator,
    S3ObjectTransport &transport,
    MetricsPublisher &publisher,
    const String &key,
    uint64_t objectSize,
    const NotifyUploadFinished &notifyUploadFinished,
    const NotifyDownloadProgress &notifyDownloadProgress,
    const NotifyDownloadFinished &notifyDownloadFinished)
{
    transport.PutObjectMultipart(
        key,
        objectSize,
        [allocator, &publisher](const MultipartTransferState::PartInfo &partInfo) {
            return s_createTemplateStream(allocator, &publisher, partInfo.sizeInBytes);
        },
        [&transport, key, notifyUploadFinished, notifyDownloadProgress, notifyDownloadFinished](
            int32_t errorCode, uint32_t numParts) {
            notifyUploadFinished(errorCode);

            if (errorCode != AWS_ERROR_SUCCESS)
            {
                return;
            }

            transport.GetObjectMultipart(
                key,
                numParts,
                [notifyDownloadProgress](const MultipartTransferState::PartInfo &partInfo, const ByteCursor &cur) {
                    (void)partInfo;
                    notifyDownloadProgress(cur.len);
                },
                [notifyDownloadFinished](int32_t errorCode) { notifyDownloadFinished(errorCode); });
        });
}
