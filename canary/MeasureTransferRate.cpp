#include "MeasureTransferRate.h"
#include "CanaryApp.h"
#include "CanaryUtil.h"
#include "MetricsPublisher.h"
#include "S3ObjectTransport.h"
#include <aws/common/clock.h>
#include <aws/common/system_info.h>
#include <execinfo.h>
#include <unistd.h>

using namespace Aws::Crt;

// TODO make handling of BodyTemplate less awkward.
size_t MeasureTransferRate::BodyTemplateSize = 500ULL * 1000ULL * 1000ULL / 8ULL;
char *MeasureTransferRate::BodyTemplate = nullptr;

const uint64_t MeasureTransferRate::SmallObjectSize = 16ULL * 1024ULL * 1024ULL;
const uint64_t MeasureTransferRate::LargeObjectSize =
    10000ULL * (500ULL * 1000ULL * 1000ULL / 8ULL); // 1ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL;
const std::chrono::milliseconds MeasureTransferRate::AllocationMetricFrequency(1000);
const uint64_t MeasureTransferRate::AllocationMetricFrequencyNS = aws_timestamp_convert(
    MeasureTransferRate::AllocationMetricFrequency.count(),
    AWS_TIMESTAMP_MILLIS,
    AWS_TIMESTAMP_NANOS,
    NULL);

bool MeasureTransferRateStream::IsValid() const noexcept
{
    return true; // TODO return !is_end_of_stream?
}

bool MeasureTransferRateStream::ReadImpl(ByteBuf &dest) noexcept
{
    (void)m_canaryApp;
    (void)m_allocator;

    size_t totalBufferSpace = dest.capacity - dest.len;
    size_t unwritten = m_partInfo->sizeInBytes - m_written;
    size_t totalToWrite = totalBufferSpace > unwritten ? unwritten : totalBufferSpace;
    size_t writtenOut = 0;

    if (m_written == 0)
    {
        m_timestamp = DateTime::Now();
    }

    while (totalToWrite)
    {
        size_t toWrite = MeasureTransferRate::BodyTemplateSize - 1 > totalToWrite
                             ? totalToWrite
                             : MeasureTransferRate::BodyTemplateSize - 1;
        ByteCursor outCur = ByteCursorFromArray((const uint8_t *)MeasureTransferRate::BodyTemplate, toWrite);

        aws_byte_buf_append(&dest, &outCur);

        writtenOut += toWrite;
        totalToWrite = totalToWrite - toWrite;
    }

    m_written += writtenOut;
    /*
        void *stackTrace[1024];
        char **stackTraceStrings;
        int stackDepth = backtrace(stackTrace, 1024);

        stackTraceStrings = backtrace_symbols(stackTrace, stackDepth);

        for(int i = 0; i < stackDepth; ++i)
        {
            AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "[%d] %s", i, stackTraceStrings[i]);
        }

        free(stackTraceStrings);


        AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "!!!!!!! Wrote %" PRId64 " bytes.....", writtenOut);
    */
    // A quick way for us to measure how much data we've actually written to S3 storage.  (This working is reliant
    // on this function only being used when we are reading data from the stream while sending that data to S3.)
    m_partInfo->AddDataUpMetric(writtenOut);

    return true;
}

Io::StreamStatus MeasureTransferRateStream::GetStatusImpl() const noexcept
{
    Io::StreamStatus status;
    status.is_end_of_stream = m_written == m_partInfo->sizeInBytes;
    status.is_valid = !status.is_end_of_stream;

    return status;
}

bool MeasureTransferRateStream::SeekImpl(Io::OffsetType offset, Io::StreamSeekBasis basis) noexcept
{
    m_written = 0;
    return true;
}

int64_t MeasureTransferRateStream::GetLengthImpl() const noexcept
{
    return m_partInfo->sizeInBytes;
}

MeasureTransferRateStream::MeasureTransferRateStream(
    CanaryApp &canaryApp,
    const std::shared_ptr<MultipartTransferState::PartInfo> &partInfo,
    Allocator *allocator)
    : m_canaryApp(canaryApp), m_partInfo(partInfo), m_allocator(allocator), m_written(0)
{
}

MeasureTransferRate::MeasureTransferRate(CanaryApp &canaryApp) : m_canaryApp(canaryApp)
{
    m_schedulingLoop = aws_event_loop_group_get_next_loop(canaryApp.eventLoopGroup.GetUnderlyingHandle());

    aws_task_init(
        &m_measureAllocationsTask,
        MeasureTransferRate::s_MeasureAllocations,
        reinterpret_cast<void *>(this),
        "MeasureTransferRate");
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
        1, LargeObjectSize, m_canaryApp.cutOffTimeLargeObjects, MeasureTransferRate::s_TransferLargeObject);
}

template <typename TPeformTransferType>
void MeasureTransferRate::PerformMeasurement(
    uint32_t maxConcurrentTransfers,
    uint64_t objectSize,
    double cutOffTime,
    const TPeformTransferType &&performTransfer)
{
    char BodyTemplateData[] = "This is a test string for use with canary testing against Amazon Simple Storage Service";
    BodyTemplate = new char[BodyTemplateSize];
    BodyTemplate[BodyTemplateSize - 1] = '\0';

    size_t totalToWrite = BodyTemplateSize;
    char *BodyTemplatePos = BodyTemplate;

    while (totalToWrite)
    {
        size_t toWrite =
            AWS_ARRAY_SIZE(BodyTemplateData) - 1 > totalToWrite ? totalToWrite : AWS_ARRAY_SIZE(BodyTemplateData) - 1;

        memcpy(BodyTemplatePos, BodyTemplateData, toWrite);

        BodyTemplatePos += toWrite;
        totalToWrite -= toWrite;
    }

    // ScheduleMeasureAllocationsTask();

    std::shared_ptr<MetricsPublisher> publisher = m_canaryApp.publisher;

    bool continueInitiatingTransfers = true;
    std::atomic<bool> forceStop(false);
    uint64_t counter = INT64_MAX;
    std::atomic<size_t> inFlightUploads(0);
    std::atomic<size_t> inFlightUploadOrDownload(0);

    time_t initialTime;
    time(&initialTime);

    while (!forceStop && (continueInitiatingTransfers || inFlightUploadOrDownload > 0))
    {
        if (counter == 0)
        {
            counter = INT64_MAX;
        }

        while (!forceStop && (continueInitiatingTransfers && inFlightUploads < maxConcurrentTransfers))
        {
            StringStream keyStream;
            keyStream << "crt-canary-obj-large-" << counter--;
            ++inFlightUploads;
            ++inFlightUploadOrDownload;
            auto key = keyStream.str();

            NotifyUploadFinished notifyUploadFinished =
                [publisher, &inFlightUploads, &inFlightUploadOrDownload, &forceStop](int32_t errorCode) {
                    --inFlightUploads;

                    if (errorCode == AWS_ERROR_SUCCESS)
                    {
                        Metric successMetric;
                        successMetric.MetricName = "SuccessfulTransfer";
                        successMetric.Unit = MetricUnit::Count;
                        successMetric.Value = 1;
                        successMetric.SetTimestampNow();

                        publisher->AddDataPoint(successMetric);
                    }
                    else
                    {
                        Metric failureMetric;
                        failureMetric.MetricName = "FailedTransfer";
                        failureMetric.Unit = MetricUnit::Count;
                        failureMetric.Value = 1;
                        failureMetric.SetTimestampNow();

                        publisher->AddDataPoint(failureMetric);
                        --inFlightUploadOrDownload;
                        forceStop = true;
                    }
                };

            NotifyDownloadProgress notifyDownloadProgress = [publisher](uint64_t dataLength) {
                (void)dataLength;
                //    publisher->AddDataDown(dataLength);
            };

            NotifyDownloadFinished notifyDownloadFinished =
                [publisher, &inFlightUploadOrDownload, &forceStop](int32_t errorCode) {
                    if (errorCode == AWS_ERROR_SUCCESS)
                    {
                        Metric successMetric;
                        successMetric.MetricName = "SuccessfulTransfer";
                        successMetric.Unit = MetricUnit::Count;
                        successMetric.Value = 1;
                        successMetric.SetTimestampNow();

                        publisher->AddDataPoint(successMetric);
                    }
                    else
                    {
                        Metric failureMetric;
                        failureMetric.MetricName = "FailedTransfer";
                        failureMetric.Unit = MetricUnit::Count;
                        failureMetric.Value = 1;
                        failureMetric.SetTimestampNow();

                        publisher->AddDataPoint(failureMetric);
                        forceStop = true;
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

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Flushing metrics...");

    publisher->WaitForLastPublish();

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Metrics flushed.");

    if (BodyTemplate != nullptr)
    {
        delete[] BodyTemplate;
        BodyTemplate = nullptr;
    }
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

    std::shared_ptr<MultipartTransferState::PartInfo> singlePart =
        MakeShared<MultipartTransferState::PartInfo>(allocator, publisher, 0, 1, 0LL, objectSize);

    transport->PutObject(
        key,
        MakeShared<MeasureTransferRateStream>(allocator, canaryApp, singlePart, allocator),
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
    std::shared_ptr<S3ObjectTransport> transport = canaryApp.transport;

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Starting upload of object %s...", key.c_str());

    transport->PutObjectMultipart(
        key,
        objectSize,
        [allocator, &canaryApp](const std::shared_ptr<MultipartTransferState::PartInfo> &partInfo) {
            return MakeShared<MeasureTransferRateStream>(allocator, canaryApp, partInfo, allocator);
        },
        [transport, key, notifyUploadFinished, notifyDownloadProgress, notifyDownloadFinished](
            int32_t errorCode, uint32_t numParts) {
            notifyUploadFinished(errorCode);

            AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Upload finished for object %s.  Starting download...", key.c_str());

            if (errorCode != AWS_ERROR_SUCCESS)
            {
                return;
            }

            transport->GetObjectMultipart(
                key,
                numParts,
                [notifyDownloadProgress](
                    const std::shared_ptr<MultipartTransferState::PartInfo> &partInfo, const ByteCursor &cur) {
                    (void)partInfo;
                    notifyDownloadProgress(cur.len);
                },
                [notifyDownloadFinished, key](int32_t errorCode) {
                    AWS_LOGF_INFO(
                        AWS_LS_CRT_CPP_CANARY,
                        "Download finished for object %s with error code %d",
                        key.c_str(),
                        errorCode);
                    notifyDownloadFinished(errorCode);
                });
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
    memMetric.SetTimestampNow();
    memMetric.MetricName = "BytesAllocated";
    publisher->AddDataPoint(memMetric);

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Emitting BytesAllocated Metric %" PRId64, (uint64_t)memMetric.Value);

    measureTransferRate->ScheduleMeasureAllocationsTask();
}
