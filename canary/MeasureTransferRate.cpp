#include "MeasureTransferRate.h"
#include "CanaryApp.h"
#include "CanaryUtil.h"
#include "MetricsPublisher.h"
#include "S3ObjectTransport.h"
#include <aws/common/clock.h>
#include <aws/common/system_info.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <condition_variable>
#include <mutex>

using namespace Aws::Crt;

// TODO make handling of BodyTemplate less awkward.
size_t MeasureTransferRate::BodyTemplateSize = 4ULL * 1024ULL;
thread_local char *BodyTemplate = nullptr;

const uint64_t MeasureTransferRate::SmallObjectSize = 5ULL * 1024ULL * 1024ULL * 1024ULL;
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

    if (!canaryApp.GetOptions().isChildProcess)
    {
        SchedulePulseMetrics();
    }
}

MeasureTransferRate::~MeasureTransferRate() {}

void MeasureTransferRate::PerformMeasurement(
    const char *filenamePrefix,
    const char *keyPrefix,
    uint32_t numTransfers,
    uint32_t numConcurrentTransfers,
    uint64_t objectSize,
    uint32_t flags,
    TransferFunction &&transferFunction)
{
    String addressKey = String() + keyPrefix + "address";
    String finishedKey = String() + keyPrefix + "finished";

    if (m_canaryApp.GetOptions().isParentProcess)
    {
        if ((flags & (uint32_t)MeasurementFlags::DontWarmDNSCache) == 0)
        {
            m_canaryApp.transport->WarmDNSCache(numConcurrentTransfers);
        }

        for (uint32_t i = 0; i < numTransfers; ++i)
        {
            const String &address = m_canaryApp.transport->GetAddressForTransfer(i);
            m_canaryApp.WriteToChildProcess(i, addressKey.c_str(), address.c_str());
        }

        for (uint32_t i = 0; i < numTransfers; ++i)
        {
            m_canaryApp.ReadFromChildProcess(i, finishedKey.c_str());
        }

        return;
    }
    else if (m_canaryApp.GetOptions().isChildProcess)
    {
        String address = m_canaryApp.ReadFromParentProcess(addressKey.c_str());

        AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Child got back address %s", address.c_str());

        m_canaryApp.transport->SeedAddressCache(address);
        m_canaryApp.transport->SpawnConnectionManagers();
    }
    else
    {
        if ((flags & (uint32_t)MeasurementFlags::DontWarmDNSCache) == 0)
        {
            m_canaryApp.transport->WarmDNSCache(numConcurrentTransfers);
        }

        m_canaryApp.transport->SpawnConnectionManagers();
    }

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Starting performance measurement.");

    std::mutex transferCompletedMutex;
    std::condition_variable transferCompletedSignal;

    std::atomic<uint32_t> numCompleted(0);
    std::atomic<uint32_t> numInProgress(0);

    uint64_t counter = INT64_MAX - (int64_t)m_canaryApp.GetOptions().childProcessIndex;

    for (uint32_t i = 0; i < numTransfers; ++i)
    {
        if (counter == 0)
        {
            counter = INT64_MAX;
        }

        StringStream keyStream;
        keyStream << filenamePrefix;

        if ((flags & (uint32_t)MeasurementFlags::NoFileSuffix) == 0)
        {
            keyStream << counter--;
        }

        String key = keyStream.str();

        ++numInProgress;

        NotifyTransferFinished notifyTransferFinished =
            [&transferCompletedSignal, &numCompleted, &numInProgress](int32_t errorCode) {
                if (errorCode != AWS_ERROR_SUCCESS)
                {
                    AWS_LOGF_INFO(
                        AWS_LS_CRT_CPP_CANARY,
                        "Transfer finished with error %d: '%s'",
                        errorCode,
                        aws_error_debug_str(errorCode));
                }

                --numInProgress;
                ++numCompleted;

                transferCompletedSignal.notify_one();
            };

        AWS_LOGF_INFO(
            AWS_LS_CRT_CPP_CANARY,
            "Beginning transfer %d - Num Concurrent:%d/%d  Total:%d/%d",
            i,
            numInProgress.load(),
            numConcurrentTransfers,
            numCompleted.load(),
            numTransfers);

        transferFunction(std::move(key), objectSize, std::move(notifyTransferFinished));

        std::unique_lock<std::mutex> guard(transferCompletedMutex);
        transferCompletedSignal.wait(guard, [&numInProgress, numConcurrentTransfers]() {
            return numInProgress.load() < numConcurrentTransfers;
        });
    }

    std::unique_lock<std::mutex> guard(transferCompletedMutex);
    transferCompletedSignal.wait(
        guard, [&numCompleted, numTransfers]() { return numCompleted.load() >= numTransfers; });

    if (m_canaryApp.GetOptions().isChildProcess)
    {
        m_canaryApp.WriteToParentProcess(finishedKey.c_str(), "done");
    }
}

void MeasureTransferRate::MeasureHttpTransfer()
{
    const String testFilename = "crt-canary-obj.txt";
    String endpoint = m_canaryApp.GetOptions().httpTestEndpoint.c_str();

    Aws::Crt::Http::HttpHeader hostHeader;
    hostHeader.name = ByteCursorFromCString("host");
    hostHeader.value = ByteCursorFromCString(endpoint.c_str());

    Http::HttpClientConnectionManagerOptions connectionManagerOptions;
    connectionManagerOptions.ConnectionOptions.HostName = endpoint;
    connectionManagerOptions.ConnectionOptions.Port = 5001;
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetConnectTimeoutMs(3000);
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetSocketType(AWS_SOCKET_STREAM);
    connectionManagerOptions.ConnectionOptions.InitialWindowSize = SIZE_MAX;

    connectionManagerOptions.ConnectionOptions.Bootstrap = &m_canaryApp.bootstrap;
    connectionManagerOptions.MaxConnections = 5000;

    std::shared_ptr<Http::HttpClientConnectionManager> connManager =
        Http::HttpClientConnectionManager::NewClientConnectionManager(connectionManagerOptions, g_allocator);

    PerformMeasurement(
        testFilename.c_str(),
        "httpTransferDown-",
        m_canaryApp.GetOptions().numTransfers,
        m_canaryApp.GetOptions().numConcurrentTransfers,
        SmallObjectSize,
        (uint32_t)MeasurementFlags::DontWarmDNSCache | (uint32_t)MeasurementFlags::NoFileSuffix,
        [this, connManager, &testFilename, &hostHeader](
            String &&key, uint64_t, NotifyTransferFinished &&notifyTransferFinished) {
            std::shared_ptr<MultipartTransferState::PartInfo> singlePart = MakeShared<MultipartTransferState::PartInfo>(
                m_canaryApp.traceAllocator, m_canaryApp.publisher, 0, 1, 0LL, SmallObjectSize);
            singlePart->AddDataDownMetric(0);

            auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
            request->AddHeader(hostHeader);
            request->SetMethod(aws_http_method_get);

            StringStream keyPathStream;
            keyPathStream << "/" << testFilename;
            String keyPath = keyPathStream.str();
            ByteCursor path = ByteCursorFromCString(keyPath.c_str());
            request->SetPath(path);

            Http::HttpRequestOptions requestOptions;
            AWS_ZERO_STRUCT(requestOptions);
            requestOptions.request = request.get();
            requestOptions.onIncomingBody = [singlePart](const Http::HttpStream &, const ByteCursor &cur) {
                singlePart->AddDataDownMetric(cur.len);
            };

            requestOptions.onStreamComplete =
                [this, keyPath, singlePart, notifyTransferFinished](Http::HttpStream &stream, int error) {
                    int errorCode = error;

                    if (errorCode == AWS_ERROR_SUCCESS)
                    {
                        if (stream.GetResponseStatusCode() != 200)
                        {
                            errorCode = AWS_ERROR_UNKNOWN;
                        }

                        aws_log_level logLevel = (errorCode != AWS_ERROR_SUCCESS) ? AWS_LL_ERROR : AWS_LL_INFO;

                        AWS_LOGF(
                            logLevel,
                            AWS_LS_CRT_CPP_CANARY,
                            "Http get finished for path %s with response status %d",
                            keyPath.c_str(),
                            stream.GetResponseStatusCode());
                    }
                    else
                    {
                        AWS_LOGF_ERROR(
                            AWS_LS_CRT_CPP_CANARY,
                            "Http get finished for path %s with error '%s'",
                            keyPath.c_str(),
                            aws_error_debug_str(errorCode));
                    }

                    m_canaryApp.publisher->AddTransferStatusDataPoint(errorCode == AWS_ERROR_SUCCESS);

                    notifyTransferFinished(errorCode);

                    singlePart->FlushDataDownMetrics();
                };

            connManager->AcquireConnection([requestOptions, notifyTransferFinished, request](
                                               std::shared_ptr<Http::HttpClientConnection> conn, int connErrorCode) {
                (void)request;

                if ((conn == nullptr || !conn->IsOpen()) && connErrorCode == AWS_ERROR_SUCCESS)
                {
                    connErrorCode = AWS_ERROR_UNKNOWN;
                }

                if (connErrorCode == AWS_ERROR_SUCCESS)
                {
                    conn->NewClientStream(requestOptions);
                }
                else
                {
                    notifyTransferFinished(connErrorCode);
                }
            });
        });

    aws_event_loop_cancel_task(m_schedulingLoop, &m_pulseMetricsTask);
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Flushing metrics...");
    m_canaryApp.publisher->WaitForLastPublish();
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Metrics flushed.");
}

void MeasureTransferRate::MeasureSmallObjectTransfer()
{
    const char *filenamePrefix = "crt-canary-obj-small-";

    PerformMeasurement(
        filenamePrefix,
        "smallObjectUp-",
        m_canaryApp.GetOptions().numTransfers,
        m_canaryApp.GetOptions().numConcurrentTransfers,
        SmallObjectSize,
        0,
        [this](String &&key, uint64_t, NotifyTransferFinished &&notifyTransferFinished) {
            std::shared_ptr<MultipartTransferState::PartInfo> singlePart = MakeShared<MultipartTransferState::PartInfo>(
                m_canaryApp.traceAllocator, m_canaryApp.publisher, 0, 1, 0LL, SmallObjectSize);

            singlePart->AddDataUpMetric(0);

            m_canaryApp.transport->PutObject(
                key,
                MakeShared<MeasureTransferRateStream>(
                    m_canaryApp.traceAllocator, m_canaryApp, singlePart, m_canaryApp.traceAllocator),
                0,
                [this, singlePart, notifyTransferFinished](int32_t errorCode, std::shared_ptr<Aws::Crt::String>) {
                    m_canaryApp.publisher->AddTransferStatusDataPoint(errorCode == AWS_ERROR_SUCCESS);
                    singlePart->FlushDataUpMetrics();
                    notifyTransferFinished(errorCode);
                });
        });

    PerformMeasurement(
        filenamePrefix,
        "smallObjectDown-",
        m_canaryApp.GetOptions().numTransfers,
        m_canaryApp.GetOptions().numConcurrentTransfers,
        SmallObjectSize,
        0,
        [this](String &&key, uint64_t, NotifyTransferFinished &&notifyTransferFinished) {
            std::shared_ptr<MultipartTransferState::PartInfo> singlePart = MakeShared<MultipartTransferState::PartInfo>(
                m_canaryApp.traceAllocator, m_canaryApp.publisher, 0, 1, 0LL, SmallObjectSize);

            singlePart->AddDataDownMetric(0);

            m_canaryApp.transport->GetObject(
                key,
                0,
                [singlePart](const Http::HttpStream &, const ByteCursor &cur) {
                    singlePart->AddDataDownMetric(cur.len);
                },
                [this, singlePart, notifyTransferFinished](int32_t errorCode) {
                    m_canaryApp.publisher->AddTransferStatusDataPoint(errorCode == AWS_ERROR_SUCCESS);
                    singlePart->FlushDataDownMetrics();
                    notifyTransferFinished(errorCode);
                });
        });

    aws_event_loop_cancel_task(m_schedulingLoop, &m_pulseMetricsTask);
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Flushing metrics...");
    m_canaryApp.publisher->WaitForLastPublish();
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Metrics flushed.");
}

void MeasureTransferRate::MeasureLargeObjectTransfer()
{
    const char *filenamePrefix = "crt-canary-obj-large-";

    PerformMeasurement(
        filenamePrefix,
        "largeObjectUp-",
        m_canaryApp.GetOptions().numTransfers,
        m_canaryApp.GetOptions().numConcurrentTransfers,
        LargeObjectSize,
        0,
        [this](String &&key, uint64_t objectSize, NotifyTransferFinished &&notifyTransferFinished) {
            AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Starting upload of object %s...", key.c_str());

            m_canaryApp.transport->PutObjectMultipart(
                key,
                objectSize,
                MeasureTransferRate::LargeObjectNumParts,
                [this](const std::shared_ptr<MultipartTransferState::PartInfo> &partInfo) {
                    return MakeShared<MeasureTransferRateStream>(
                        m_canaryApp.traceAllocator, m_canaryApp, partInfo, m_canaryApp.traceAllocator);
                },
                [key, notifyTransferFinished](int32_t errorCode, uint32_t) {
                    AWS_LOGF_INFO(
                        AWS_LS_CRT_CPP_CANARY,
                        "Upload finished for object %s with error code %d",
                        key.c_str(),
                        errorCode);

                    notifyTransferFinished(errorCode);
                });
        });

    PerformMeasurement(
        filenamePrefix,
        "largeObjectDown-",
        m_canaryApp.GetOptions().numConcurrentTransfers,
        S3ObjectTransport::MaxStreams,
        LargeObjectSize,
        0,
        [this](String &&key, uint64_t, NotifyTransferFinished &&notifyTransferFinished) {
            AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Starting download of object %s...", key.c_str());

            m_canaryApp.transport->GetObjectMultipart(
                key,
                MeasureTransferRate::LargeObjectNumParts,
                [](const std::shared_ptr<MultipartTransferState::PartInfo> &, const ByteCursor &) {},
                [notifyTransferFinished, key](int32_t errorCode) {
                    AWS_LOGF_INFO(
                        AWS_LS_CRT_CPP_CANARY,
                        "Download finished for object %s with error code %d",
                        key.c_str(),
                        errorCode);
                    notifyTransferFinished(errorCode);
                });
        });

    aws_event_loop_cancel_task(m_schedulingLoop, &m_pulseMetricsTask);
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Flushing metrics...");
    m_canaryApp.publisher->WaitForLastPublish();
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Metrics flushed.");
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
