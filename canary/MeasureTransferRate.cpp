#include "MeasureTransferRate.h"
#include "CanaryApp.h"
#include "CanaryUtil.h"
#include "MetricsPublisher.h"
#include "S3ObjectTransport.h"
#include <aws/common/clock.h>
#include <aws/common/system_info.h>
#include <aws/crt/http/HttpConnection.h>
//#include <execinfo.h>
//#include <unistd.h>

using namespace Aws::Crt;

// TODO make handling of BodyTemplate less awkward.
size_t MeasureTransferRate::BodyTemplateSize = 4ULL * 1024ULL;
thread_local char *BodyTemplate = nullptr;

const uint64_t MeasureTransferRate::SmallObjectSize = 16ULL * 1024ULL * 1024ULL;
const uint32_t MeasureTransferRate::LargeObjectNumParts = 8192;
const uint64_t MeasureTransferRate::LargeObjectSize =
    (uint64_t)MeasureTransferRate::LargeObjectNumParts * (128ULL * 1024ULL * 1024ULL);
const std::chrono::milliseconds MeasureTransferRate::AllocationMetricFrequency(5000);
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
    if (BodyTemplate == nullptr)
    {
        char BodyTemplateData[] =
            "This is a test string for use with canary testing against Amazon Simple Storage Service";

        BodyTemplate = new char[MeasureTransferRate::BodyTemplateSize];
        BodyTemplate[MeasureTransferRate::BodyTemplateSize - 1] = '\0';

        size_t totalToWrite = MeasureTransferRate::BodyTemplateSize;
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
        ByteCursor outCur = ByteCursorFromArray((const uint8_t *)BodyTemplate, toWrite);

        aws_byte_buf_append(&dest, &outCur);

        writtenOut += toWrite;
        totalToWrite = totalToWrite - toWrite;
    }

    m_written += writtenOut;

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

bool MeasureTransferRateStream::SeekImpl(Io::OffsetType, Io::StreamSeekBasis) noexcept
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
    (void)m_canaryApp;
    (void)m_allocator;
}

MeasureTransferRate::MeasureTransferRate(CanaryApp &canaryApp) : m_canaryApp(canaryApp)
{
    m_schedulingLoop = aws_event_loop_group_get_next_loop(canaryApp.eventLoopGroup.GetUnderlyingHandle());

    aws_task_init(
        &m_pulseMetricsTask,
        MeasureTransferRate::s_PulseMetricsTask,
        reinterpret_cast<void *>(this),
        "MeasureTransferRate");

    SchedulePulseMetrics();
}

template <typename TPeformTransferType>
void MeasureTransferRate::PerformMeasurement(
    const char *filenamePrefix,
    uint32_t maxConcurrentTransfers,
    uint64_t objectSize,
    double cutOffTime,
    bool transferStatusMetricPerWorkload,
    const TPeformTransferType &&performTransfer)
{
    std::shared_ptr<MetricsPublisher> publisher = m_canaryApp.publisher;

    bool continueInitiatingTransfers = true;
    std::atomic<bool> forceStop(false);
    uint64_t counter = INT64_MAX;

    std::atomic<uint32_t> numInProgress(0);

    time_t initialTime;
    time(&initialTime);

    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY, "Starting performance measurement.  Measuring for at least %f seconds.", cutOffTime);

    while (!forceStop && (continueInitiatingTransfers || m_canaryApp.transport->GetOpenConnectionCount() > 0))
    {
        if (counter == 0)
        {
            counter = INT64_MAX;
        }

        while (
            !forceStop &&
            (continueInitiatingTransfers &&
             numInProgress <
                 maxConcurrentTransfers)) // m_canaryApp.transport->GetOpenConnectionCount() < maxConcurrentTransfers))
        {
            StringStream keyStream;
            keyStream << filenamePrefix << counter--;
            auto key = keyStream.str();
            ++numInProgress;

            NotifyUploadFinished notifyUploadFinished = [publisher,
                                                         transferStatusMetricPerWorkload](int32_t errorCode) {
                if (errorCode == AWS_ERROR_SUCCESS)
                {
                    if (transferStatusMetricPerWorkload)
                    {
                        Metric successMetric;
                        successMetric.MetricName = "SuccessfulTransfer";
                        successMetric.Unit = MetricUnit::Count;
                        successMetric.Value = 1;
                        successMetric.SetTimestampNow();

                        publisher->AddDataPoint(successMetric);
                    }
                }
                else
                {
                    if (transferStatusMetricPerWorkload)
                    {
                        Metric failureMetric;
                        failureMetric.MetricName = "FailedTransfer";
                        failureMetric.Unit = MetricUnit::Count;
                        failureMetric.Value = 1;
                        failureMetric.SetTimestampNow();

                        publisher->AddDataPoint(failureMetric);
                    }
                    // forceStop = true;
                }
            };

            NotifyDownloadProgress notifyDownloadProgress = [publisher](uint64_t) {
                // AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Received %" PRId64 " bytes", dataLength);
            };

            NotifyDownloadFinished notifyDownloadFinished =
                [publisher, transferStatusMetricPerWorkload, &numInProgress](int32_t errorCode) {
                    if (transferStatusMetricPerWorkload)
                    {
                        if (errorCode == AWS_ERROR_SUCCESS)
                        {
                            Metric successMetric;
                            successMetric.MetricName = "SuccessfulTransfer";
                            successMetric.Unit = MetricUnit::Count;
                            successMetric.Value = 1;
                            successMetric.SetTimestampNow();

                            //publisher->AddDataPoint(successMetric);
                        }
                        else
                        {
                            Metric failureMetric;
                            failureMetric.MetricName = "FailedTransfer";
                            failureMetric.Unit = MetricUnit::Count;
                            failureMetric.Value = 1;
                            failureMetric.SetTimestampNow();

                            //publisher->AddDataPoint(failureMetric);
                            // forceStop = true;
                        }
                    }

                    --numInProgress;
                };

            performTransfer(
                *this, key, objectSize, notifyUploadFinished, notifyDownloadProgress, notifyDownloadFinished);
        }

        time_t currentTime;
        time(&currentTime);
        double elapsedSeconds = difftime(currentTime, initialTime);
        bool wasInitiatingTransfers = continueInitiatingTransfers;
        continueInitiatingTransfers = elapsedSeconds <= cutOffTime;
        if (!continueInitiatingTransfers && wasInitiatingTransfers)
        {
            AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Minimum running time has elapsed.  No longer initiating transfers.");
        }
    }

    aws_event_loop_cancel_task(m_schedulingLoop, &m_pulseMetricsTask);

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Flushing metrics...");

    publisher->WaitForLastPublish();

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Metrics flushed.");
}

void MeasureTransferRate::MeasureSmallObjectTransfer()
{
    uint32_t threadCount = 32; // static_cast<uint32_t>(aws_system_info_processor_count());
    uint32_t maxInFlight = threadCount * 30;

    m_canaryApp.transport->WarmDNSCache();

    PerformMeasurement(
        "crt-canary-obj-small-",
        maxInFlight,
        SmallObjectSize,
        m_canaryApp.cutOffTimeSmallObjects,
        true,
        MeasureTransferRate::s_TransferSmallObject);
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
        0,
        [singlePart, transport, key, notifyUploadFinished, notifyDownloadProgress, notifyDownloadFinished](
            int32_t errorCode, std::shared_ptr<Aws::Crt::String>) {
            notifyUploadFinished(errorCode);

            if (errorCode != AWS_ERROR_SUCCESS)
            {
                notifyDownloadFinished(errorCode);
                return;
            }

            singlePart->FlushDataUpMetrics();

			notifyDownloadFinished(AWS_ERROR_SUCCESS);
            /*
            transport->GetObject(
                key,
                0,
                [singlePart, notifyDownloadProgress](const Http::HttpStream &, const ByteCursor &cur) {
                    singlePart->AddDataDownMetric(cur.len);
                    notifyDownloadProgress(cur.len);
                },
                [singlePart, notifyDownloadFinished](int32_t errorCode) {
                    singlePart->FlushDataDownMetrics();
                    notifyDownloadFinished(errorCode);
                });*/
        });
}

void MeasureTransferRate::MeasureLargeObjectTransfer()
{
    m_canaryApp.transport->WarmDNSCache();

    PerformMeasurement(
        "crt-canary-obj-large-",
        1,
        LargeObjectSize,
        m_canaryApp.cutOffTimeLargeObjects,
        false,
        MeasureTransferRate::s_TransferLargeObject);
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
        MeasureTransferRate::LargeObjectNumParts,
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

void MeasureTransferRate::SchedulePulseMetrics()
{
    uint64_t now = 0;
    aws_event_loop_current_clock_time(m_schedulingLoop, &now);
    aws_event_loop_schedule_task_future(
        m_schedulingLoop, &m_pulseMetricsTask, now + MeasureTransferRate::AllocationMetricFrequencyNS);
}

void MeasureTransferRate::s_PulseMetricsTask(aws_task *task, void *arg, aws_task_status status)
{
    (void)task;

    if (status != AWS_TASK_STATUS_RUN_READY)
    {
        return;
    }

    MeasureTransferRate *measureTransferRate = reinterpret_cast<MeasureTransferRate *>(arg);
    CanaryApp &canaryApp = measureTransferRate->m_canaryApp;
    std::shared_ptr<MetricsPublisher> publisher = canaryApp.publisher;
    std::shared_ptr<S3ObjectTransport> transport = canaryApp.transport;

    /*
    Allocator *traceAllocator = canaryApp.traceAllocator;


    Metric memMetric;
    memMetric.Unit = MetricUnit::Bytes;
    memMetric.Value = (double)aws_mem_tracer_bytes(traceAllocator);
    memMetric.SetTimestampNow();
    memMetric.MetricName = "BytesAllocated";
    publisher->AddDataPoint(memMetric);

    AWS_LOGF_DEBUG(AWS_LS_CRT_CPP_CANARY, "Emitting BytesAllocated Metric %" PRId64, (uint64_t)memMetric.Value);
    */

    {
        size_t openConnectionCount = transport->GetOpenConnectionCount();

        Metric connMetric;
        connMetric.Unit = MetricUnit::Count;
        connMetric.Value = (double)openConnectionCount;
        connMetric.SetTimestampNow();
        connMetric.MetricName = "NumConnections";
        publisher->AddDataPoint(connMetric);

        AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Open-connections:%d", (uint32_t)openConnectionCount);
    }

    {
        const Aws::Crt::String &s3Endpoint = transport->GetEndpoint();
        size_t s3AddressCount = canaryApp.defaultHostResolver.GetHostAddressCount(s3Endpoint);

        Metric s3AddressCountMetric;
        s3AddressCountMetric.Unit = MetricUnit::Count;
        s3AddressCountMetric.Value = (double)s3AddressCount;
        s3AddressCountMetric.SetTimestampNow();
        s3AddressCountMetric.MetricName = "S3AddressCount";
        publisher->AddDataPoint(s3AddressCountMetric);

        AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Number-of-s3-addresses:%d", (uint32_t)s3AddressCount);
    }

    {
        size_t uniqueEndpointsUsed = transport->GetUniqueEndpointsUsedCount();

        Metric uniqueEndpointsUsedMetric;
        uniqueEndpointsUsedMetric.Unit = MetricUnit::Count;
        uniqueEndpointsUsedMetric.Value = (double)uniqueEndpointsUsed;
        uniqueEndpointsUsedMetric.SetTimestampNow();
        uniqueEndpointsUsedMetric.MetricName = "UniqueEndpointsUsed";
        publisher->AddDataPoint(uniqueEndpointsUsedMetric);

        AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Number-of-unique-endpoints-used:%d", (uint32_t)uniqueEndpointsUsed);
    }

    measureTransferRate->SchedulePulseMetrics();
}
