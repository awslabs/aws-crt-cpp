#include "MeasureTransferRate.h"
#include "CanaryApp.h"
#include "CanaryUtil.h"
#include "MetricsPublisher.h"
#include "S3ObjectTransport.h"
#include <aws/common/clock.h>
#include <aws/common/system_info.h>

using namespace Aws::Crt;

const char MeasureTransferRate::BodyTemplate[] =
    "This is a test string for use with canary testing against Amazon Simple Storage Service";

const size_t MeasureTransferRate::SmallObjectSize = 16 * 1024 * 1024;
const size_t MeasureTransferRate::LargeObjectSize = 10 * S3ObjectTransport::MaxPartSizeBytes;
const std::chrono::milliseconds MeasureTransferRate::AllocationMetricFrequency(250);
const uint64_t MeasureTransferRate::AllocationMetricFrequencyNS = aws_timestamp_convert(
    MeasureTransferRate::AllocationMetricFrequency.count(),
    AWS_TIMESTAMP_SECS,
    AWS_TIMESTAMP_NANOS,
    NULL);

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

    // A quick way for us to measure how much data we've actually written to S3 storage.  (This working is reliant
    // on this function only being used when we are reading data from the stream while sending that data to S3.)
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

MeasureTransferRate::MeasureTransferRate(CanaryApp &canaryApp) : m_canaryApp(canaryApp)
{
    m_schedulingLoop = aws_event_loop_group_get_next_loop(canaryApp.eventLoopGroup.GetUnderlyingHandle());

    AWS_ZERO_STRUCT(m_measureAllocationsTask);
    aws_task_init(
        &m_measureAllocationsTask, MeasureTransferRate::s_MeasureAllocations, reinterpret_cast<void *>(this), nullptr);
}

void MeasureTransferRate::MeasureSmallObjectTransfer()
{
    uint32_t threadCount = static_cast<uint32_t>(aws_system_info_processor_count());
    uint32_t maxInFlight = threadCount * 10;

    PerformMeasurement(
        maxInFlight, SmallObjectSize, m_canaryApp.cutOffTimeSmallObjects, MeasureTransferRate::s_TransferSmallObject);
}

void MeasureTransferRate::MeasureLargeObjectTransfer()
{
    PerformMeasurement(
        2, LargeObjectSize, m_canaryApp.cutOffTimeLargeObjects, MeasureTransferRate::s_TransferLargeObject);
}

template <typename TPeformTransferType>
void MeasureTransferRate::PerformMeasurement(
    uint32_t maxConcurrentTransfers,
    uint64_t objectSize,
    double cutOffTime,
    const TPeformTransferType &&performTransfer)
{
    ScheduleMeasureAllocationsTask();

    std::shared_ptr<MetricsPublisher> publisher = m_canaryApp.publisher;

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
                [publisher, &inFlightUploads, &inFlightUploadOrDownload](int32_t errorCode) {
                    --inFlightUploads;

                    if (errorCode == AWS_ERROR_SUCCESS)
                    {
                        Metric successMetric;
                        successMetric.MetricName = "SuccessfulTransfer";
                        successMetric.Unit = MetricUnit::Count;
                        successMetric.Value = 1;
                        successMetric.Timestamp = DateTime::Now();

                        publisher->AddDataPoint(successMetric);
                    }
                    else
                    {
                        Metric failureMetric;
                        failureMetric.MetricName = "FailedTransfer";
                        failureMetric.Unit = MetricUnit::Count;
                        failureMetric.Value = 1;
                        failureMetric.Timestamp = DateTime::Now();

                        publisher->AddDataPoint(failureMetric);

                        --inFlightUploadOrDownload;
                    }
                };

            NotifyDownloadProgress notifyDownloadProgress = [publisher](uint64_t dataLength) {
                std::shared_ptr<Metric> downMetric = MakeShared<Metric>(g_allocator);
                downMetric->MetricName = "BytesDown";
                downMetric->Unit = MetricUnit::Bytes;
                downMetric->Value = static_cast<double>(dataLength);
                downMetric->Timestamp = DateTime::Now();
                publisher->AddDataPoint(*downMetric);
            };

            NotifyDownloadFinished notifyDownloadFinished = [publisher, &inFlightUploadOrDownload](int32_t errorCode) {
                if (errorCode == AWS_ERROR_SUCCESS)
                {
                    Metric successMetric;
                    successMetric.MetricName = "SuccessfulTransfer";
                    successMetric.Unit = MetricUnit::Count;
                    successMetric.Value = 1;
                    successMetric.Timestamp = DateTime::Now();

                    publisher->AddDataPoint(successMetric);
                }
                else
                {
                    Metric failureMetric;
                    failureMetric.MetricName = "FailedTransfer";
                    failureMetric.Unit = MetricUnit::Count;
                    failureMetric.Value = 1;
                    failureMetric.Timestamp = DateTime::Now();

                    publisher->AddDataPoint(failureMetric);
                }

                --inFlightUploadOrDownload;
            };

            performTransfer(
                *this, key, objectSize, notifyUploadFinished, notifyDownloadProgress, notifyDownloadFinished);
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));

        time_t currentTime;
        time(&currentTime);
        double elapsedSeconds = difftime(currentTime, initialTime);
        continueInitiatingTransfers = elapsedSeconds <= cutOffTime;
    }

    aws_event_loop_cancel_task(m_schedulingLoop, &m_measureAllocationsTask);

    publisher->WaitForLastPublish();
}

void MeasureTransferRate::s_TransferSmallObject(
    MeasureTransferRate &measureTransferRate,
    const String &key,
    uint64_t objectSize,
    const NotifyUploadFinished &notifyUploadFinished,
    const NotifyDownloadProgress &notifyDownloadProgress,
    const NotifyDownloadFinished &notifyDownloadFinished)
{
    CanaryApp &canaryApp = measureTransferRate.m_canaryApp;
    Allocator *allocator = canaryApp.traceAllocator;
    std::shared_ptr<MetricsPublisher> publisher = canaryApp.publisher;
    std::shared_ptr<S3ObjectTransport> transport = canaryApp.transport;

    transport->PutObject(
        key,
        s_createTemplateStream(allocator, publisher.get(), objectSize),
        [transport, key, notifyUploadFinished, notifyDownloadProgress, notifyDownloadFinished](
            int32_t errorCode, std::shared_ptr<Aws::Crt::String>) {
            notifyUploadFinished(errorCode);

            if (errorCode != AWS_ERROR_SUCCESS)
            {
                notifyDownloadFinished(AWS_ERROR_UNKNOWN);
                return;
            }

            transport->GetObject(
                key,
                [notifyDownloadProgress](const Http::HttpStream &, const ByteCursor &cur) {
                    notifyDownloadProgress(cur.len);
                },
                [notifyDownloadFinished](int32_t errorCode) { notifyDownloadFinished(errorCode); });
        });
}

void MeasureTransferRate::s_TransferLargeObject(
    MeasureTransferRate &measureTransferRate,
    const String &key,
    uint64_t objectSize,
    const NotifyUploadFinished &notifyUploadFinished,
    const NotifyDownloadProgress &notifyDownloadProgress,
    const NotifyDownloadFinished &notifyDownloadFinished)
{
    CanaryApp &canaryApp = measureTransferRate.m_canaryApp;
    Allocator *allocator = canaryApp.traceAllocator;
    std::shared_ptr<MetricsPublisher> publisher = canaryApp.publisher;
    std::shared_ptr<S3ObjectTransport> transport = canaryApp.transport;

    transport->PutObjectMultipart(
        key,
        objectSize,
        [allocator, publisher](const MultipartTransferState::PartInfo &partInfo) {
            return s_createTemplateStream(allocator, publisher.get(), partInfo.sizeInBytes);
        },
        [transport, key, notifyUploadFinished, notifyDownloadProgress, notifyDownloadFinished](
            int32_t errorCode, uint32_t numParts) {
            notifyUploadFinished(errorCode);

            if (errorCode != AWS_ERROR_SUCCESS)
            {
                return;
            }

            transport->GetObjectMultipart(
                key,
                numParts,
                [notifyDownloadProgress](const MultipartTransferState::PartInfo &partInfo, const ByteCursor &cur) {
                    (void)partInfo;
                    notifyDownloadProgress(cur.len);
                },
                [notifyDownloadFinished](int32_t errorCode) { notifyDownloadFinished(errorCode); });
        });
}

void MeasureTransferRate::ScheduleMeasureAllocationsTask()
{
    uint64_t now = 0;
    aws_event_loop_current_clock_time(m_schedulingLoop, &now);
    aws_event_loop_schedule_task_future(
        m_schedulingLoop, &m_measureAllocationsTask, now + MeasureTransferRate::AllocationMetricFrequencyNS);
}

void MeasureTransferRate::s_MeasureAllocations(aws_task *task, void *arg, aws_task_status status)
{
    (void)task;

    if (status != AWS_TASK_STATUS_RUN_READY)
    {
        return;
    }

    MeasureTransferRate *measureTransferRate = reinterpret_cast<MeasureTransferRate *>(arg);
    CanaryApp &canaryApp = measureTransferRate->m_canaryApp;
    Allocator *traceAllocator = canaryApp.traceAllocator;
    std::shared_ptr<MetricsPublisher> publisher = canaryApp.publisher;

    Metric memMetric;
    memMetric.Unit = MetricUnit::Bytes;
    memMetric.Value = (double)aws_mem_tracer_bytes(traceAllocator);
    memMetric.Timestamp = DateTime::Now();
    memMetric.MetricName = "BytesAllocated";
    publisher->AddDataPoint(memMetric);

    measureTransferRate->ScheduleMeasureAllocationsTask();
}
