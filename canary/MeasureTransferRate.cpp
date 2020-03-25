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

    std::shared_ptr<S3ObjectTransport> transport = (flags & (uint32_t)MeasurementFlags::SecondaryTransport) == 0
                                                       ? m_canaryApp.transport
                                                       : m_canaryApp.transportSecondary;

    if (m_canaryApp.GetOptions().isParentProcess)
    {
        if ((flags & (uint32_t)MeasurementFlags::DontWarmDNSCache) == 0)
        {
            transport->WarmDNSCache(numConcurrentTransfers);
        }

        for (uint32_t i = 0; i < numTransfers; ++i)
        {
            const String &address = transport->GetAddressForTransfer(i);
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

        transport->SeedAddressCache(address);
        transport->SpawnConnectionManagers();
    }
    else
    {
        if ((flags & (uint32_t)MeasurementFlags::DontWarmDNSCache) == 0)
        {
            transport->WarmDNSCache(numConcurrentTransfers);
        }

        transport->SpawnConnectionManagers();
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

        transferFunction(i, std::move(key), objectSize, std::move(notifyTransferFinished));

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
    connectionManagerOptions.ConnectionOptions.Port = m_canaryApp.GetOptions().sendEncrypted ? 443 : 5001;
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetConnectTimeoutMs(3000);
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetSocketType(AWS_SOCKET_STREAM);
    connectionManagerOptions.ConnectionOptions.InitialWindowSize = SIZE_MAX;

    if (m_canaryApp.GetOptions().sendEncrypted)
    {
        aws_byte_cursor serverName = ByteCursorFromCString(endpoint.c_str());
        auto connOptions = m_canaryApp.tlsContext.NewConnectionOptions();
        connOptions.SetServerName(serverName);
        connectionManagerOptions.ConnectionOptions.TlsOptions = connOptions;
    }

    connectionManagerOptions.ConnectionOptions.Bootstrap = &m_canaryApp.bootstrap;
    connectionManagerOptions.MaxConnections = 5000;

    std::shared_ptr<Http::HttpClientConnectionManager> connManager =
        Http::HttpClientConnectionManager::NewClientConnectionManager(connectionManagerOptions, g_allocator);

    PerformMeasurement(
        testFilename.c_str(),
        "httpTransferDown-",
        m_canaryApp.GetOptions().numDownTransfers,
        m_canaryApp.GetOptions().numDownConcurrentTransfers,
        SmallObjectSize,
        (uint32_t)MeasurementFlags::DontWarmDNSCache | (uint32_t)MeasurementFlags::NoFileSuffix,
        [this, connManager, &testFilename, &hostHeader](
            uint32_t, String &&key, uint64_t, NotifyTransferFinished &&notifyTransferFinished) {
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
                [keyPath, singlePart, notifyTransferFinished](Http::HttpStream &stream, int error) {
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
    m_canaryApp.publisher->SchedulePublish();
    m_canaryApp.publisher->WaitForLastPublish();
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Metrics flushed.");

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Uploading backup...");
    m_canaryApp.publisher->UploadBackup();
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Uploading backup finished.");
}

void MeasureTransferRate::MeasureSmallObjectTransfer()
{
    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY,
        "Measurements: %d,%d %d,%d",
        m_canaryApp.GetOptions().numUpTransfers,
        m_canaryApp.GetOptions().numUpConcurrentTransfers,
        m_canaryApp.GetOptions().numDownTransfers,
        m_canaryApp.GetOptions().numDownConcurrentTransfers);

    Vector<std::shared_ptr<MultipartTransferState::PartInfo>> uploads;
    Vector<std::shared_ptr<MultipartTransferState::PartInfo>> downloads;

    if (!m_canaryApp.GetOptions().downloadOnly)
    {
        for (uint32_t i = 0; i < m_canaryApp.GetOptions().numUpTransfers; ++i)
        {
            std::shared_ptr<MultipartTransferState::PartInfo> singlePart = MakeShared<MultipartTransferState::PartInfo>(
                m_canaryApp.traceAllocator, m_canaryApp.publisher, 0, 1, 0LL, SmallObjectSize);

            uploads.push_back(singlePart);
        }

        PerformMeasurement(
            "crt-canary-obj-small-",
            "smallObjectUp-",
            m_canaryApp.GetOptions().numUpTransfers,
            m_canaryApp.GetOptions().numUpConcurrentTransfers,
            SmallObjectSize,
            0,
            [this, &uploads](
                uint32_t transferIndex, String &&key, uint64_t, NotifyTransferFinished &&notifyTransferFinished) {
                std::shared_ptr<MultipartTransferState::PartInfo> singlePart = uploads[transferIndex];

                singlePart->AddDataUpMetric(0);

                m_canaryApp.transport->PutObject(
                    key,
                    MakeShared<MeasureTransferRateStream>(
                        m_canaryApp.traceAllocator, m_canaryApp, singlePart, m_canaryApp.traceAllocator),
                    0,
                    [singlePart, notifyTransferFinished](int32_t errorCode, std::shared_ptr<Aws::Crt::String>) {
                        singlePart->transferSuccess = errorCode == AWS_ERROR_SUCCESS;
                        notifyTransferFinished(errorCode);
                    });
            });

        for (uint32_t i = 0; i < m_canaryApp.GetOptions().numUpTransfers; ++i)
        {
            uploads[i]->FlushDataUpMetrics();
        }

        AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Flushing metrics...");
        m_canaryApp.publisher->SchedulePublish();
        m_canaryApp.publisher->WaitForLastPublish();
        AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Metrics flushed.");
    }

    for (uint32_t i = 0; i < m_canaryApp.GetOptions().numDownTransfers; ++i)
    {
        std::shared_ptr<MultipartTransferState::PartInfo> singlePart = MakeShared<MultipartTransferState::PartInfo>(
            m_canaryApp.traceAllocator, m_canaryApp.publisher, 0, 1, 0LL, SmallObjectSize);

        downloads.emplace_back(singlePart);
    }

    PerformMeasurement(
        "crt-canary-obj.txt",
        "smallObjectDown-",
        m_canaryApp.GetOptions().numDownTransfers,
        m_canaryApp.GetOptions().numDownConcurrentTransfers,
        SmallObjectSize,
        (uint32_t)MeasurementFlags::NoFileSuffix | (uint32_t)MeasurementFlags::SecondaryTransport,
        [this,
         &downloads](uint32_t transferIndex, String &&key, uint64_t, NotifyTransferFinished &&notifyTransferFinished) {
            std::shared_ptr<MultipartTransferState::PartInfo> singlePart = downloads[transferIndex];

            singlePart->AddDataDownMetric(0);

            m_canaryApp.transportSecondary->GetObject(
                key,
                0,
                [singlePart](const Http::HttpStream &, const ByteCursor &cur) {
                    singlePart->AddDataDownMetric(cur.len);
                },
                [singlePart, notifyTransferFinished](int32_t errorCode) {
                    singlePart->transferSuccess = errorCode == AWS_ERROR_SUCCESS;
                    notifyTransferFinished(errorCode);
                });
        });

    for (uint32_t i = 0; i < m_canaryApp.GetOptions().numDownTransfers; ++i)
    {
        downloads[i]->FlushDataDownMetrics();
    }

    aws_event_loop_cancel_task(m_schedulingLoop, &m_pulseMetricsTask);
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Flushing metrics...");
    m_canaryApp.publisher->SchedulePublish();
    m_canaryApp.publisher->WaitForLastPublish();
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Metrics flushed.");

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Uploading backup...");
    m_canaryApp.publisher->UploadBackup();
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Uploading backup finished.");
}

void MeasureTransferRate::MeasureLargeObjectTransfer()
{
    const char *filenamePrefix = "crt-canary-obj-large-";

    PerformMeasurement(
        filenamePrefix,
        "largeObjectUp-",
        m_canaryApp.GetOptions().numUpTransfers,
        m_canaryApp.GetOptions().numUpConcurrentTransfers,
        LargeObjectSize,
        0,
        [this](uint32_t, String &&key, uint64_t objectSize, NotifyTransferFinished &&notifyTransferFinished) {
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
        m_canaryApp.GetOptions().numDownTransfers,
        m_canaryApp.GetOptions().numDownConcurrentTransfers,
        LargeObjectSize,
        0,
        [this](uint32_t, String &&key, uint64_t, NotifyTransferFinished &&notifyTransferFinished) {
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
    m_canaryApp.publisher->SchedulePublish();
    m_canaryApp.publisher->WaitForLastPublish();
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Metrics flushed.");

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Uploading backup...");
    m_canaryApp.publisher->UploadBackup();
    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Uploading backup finished.");
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
    memMetric.Name = MetricName::BytesAllocated;
    publisher->AddDataPoint(memMetric);

    AWS_LOGF_DEBUG(AWS_LS_CRT_CPP_CANARY, "Emitting BytesAllocated Metric %" PRId64, (uint64_t)memMetric.Value);
    */
    /*
        {
            size_t openConnectionCount = transport->GetOpenConnectionCount();

            Metric connMetric(MetricName::NumConnections, MetricUnit::Count, (double)openConnectionCount);

            publisher->AddDataPoint(connMetric);

            AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Open-connections:%d", (uint32_t)openConnectionCount);
        }
    */

    {
        const Aws::Crt::String &s3Endpoint = transport->GetEndpoint();

        size_t s3AddressCount = canaryApp.defaultHostResolver.GetHostAddressCount(s3Endpoint, AWS_GET_HOST_ADDRESS_COUNT_RECORD_TYPE_A);

        Metric s3AddressCountMetric(MetricName::S3AddressCount, MetricUnit::Count, (double)s3AddressCount);

        publisher->AddDataPoint(s3AddressCountMetric);

        AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Number-of-s3-addresses:%d", (uint32_t)s3AddressCount);
    }

    /*
    {
        aws_event_loop_group *eventLoopGroup = canaryApp.eventLoopGroup.GetUnderlyingHandle();
        size_t numEventLoops = aws_array_list_length(&eventLoopGroup->event_loops);

        size_t totalTickElapsedTimeMS = 0;
        size_t totalTaskRunElapsedTimeMS = 0;
        size_t totalIOSubs = 0;
        size_t minTickElapsedTimeMS = 0;
        size_t minTaskRunElapsedTimeMS = 0;
        size_t maxTickElapsedTimeMS = 0;
        size_t maxTaskRunElapsedTimeMS = 0;

        for (size_t i = 0; i < numEventLoops; ++i)
        {
            aws_event_loop *eventLoop = nullptr;
            aws_array_list_get_at(&eventLoopGroup->event_loops, (void *)&eventLoop, i);

            size_t tickElapsedTimeNS = aws_atomic_load_int(&eventLoop->tick_elapsed_time);
            size_t taskRunElapsedTimeNS = aws_atomic_load_int(&eventLoop->task_elapsed_time);
            size_t numIOSubs = aws_atomic_load_int(&eventLoop->num_io_subscriptions);

            size_t tickElapsedTimeMS =
                aws_timestamp_convert(tickElapsedTimeNS, AWS_TIMESTAMP_NANOS, AWS_TIMESTAMP_MILLIS, nullptr);
            size_t taskRunElapsedTimeMS =
                aws_timestamp_convert(taskRunElapsedTimeNS, AWS_TIMESTAMP_NANOS, AWS_TIMESTAMP_MILLIS, nullptr);

            if (i == 0)
            {
                minTickElapsedTimeMS = tickElapsedTimeMS;
                minTaskRunElapsedTimeMS = taskRunElapsedTimeMS;
                maxTickElapsedTimeMS = tickElapsedTimeMS;
                maxTaskRunElapsedTimeMS = taskRunElapsedTimeMS;
            }
            else
            {
                minTickElapsedTimeMS = std::min(minTickElapsedTimeMS, tickElapsedTimeMS);
                minTaskRunElapsedTimeMS = std::min(minTaskRunElapsedTimeMS, taskRunElapsedTimeMS);
                maxTickElapsedTimeMS = std::max(maxTickElapsedTimeMS, tickElapsedTimeMS);
                maxTaskRunElapsedTimeMS = std::max(maxTaskRunElapsedTimeMS, taskRunElapsedTimeMS);
            }

            totalTickElapsedTimeMS += tickElapsedTimeMS;
            totalTaskRunElapsedTimeMS += taskRunElapsedTimeMS;
            totalIOSubs += numIOSubs;
        }

        Metric avgEventLoopGroupTickElapsed(
            MetricName::AvgEventLoopGroupTickElapsed,
            MetricUnit::Milliseconds,
            (double)totalTickElapsedTimeMS / (double)numEventLoops);
        Metric avgEventLoopGroupTaskRunElapsed(
            MetricName::AvgEventLoopTaskRunElapsed,
            MetricUnit::Milliseconds,
            (double)totalTaskRunElapsedTimeMS / (double)numEventLoops);
        Metric minEventLoopGroupTickElapsed(
            MetricName::MinEventLoopGroupTickElapsed, MetricUnit::Milliseconds, (double)minTickElapsedTimeMS);
        Metric minEventLoopGroupTaskRunElapsed(
            MetricName::MinEventLoopTaskRunElapsed, MetricUnit::Milliseconds, (double)minTaskRunElapsedTimeMS);
        Metric maxEventLoopGroupTickElapsed(
            MetricName::MaxEventLoopGroupTickElapsed, MetricUnit::Milliseconds, (double)maxTickElapsedTimeMS);
        Metric maxEventLoopGroupTaskRunElapsed(
            MetricName::MaxEventLoopTaskRunElapsed, MetricUnit::Milliseconds, (double)maxTaskRunElapsedTimeMS);

        Metric numIOSubs(MetricName::NumIOSubs, MetricUnit::Count, (double)totalIOSubs);

        publisher->AddDataPoint(avgEventLoopGroupTickElapsed);
        publisher->AddDataPoint(avgEventLoopGroupTaskRunElapsed);
        publisher->AddDataPoint(minEventLoopGroupTickElapsed);
        publisher->AddDataPoint(minEventLoopGroupTaskRunElapsed);
        publisher->AddDataPoint(maxEventLoopGroupTickElapsed);
        publisher->AddDataPoint(maxEventLoopGroupTaskRunElapsed);
        publisher->AddDataPoint(numIOSubs);
    }
    */

    /*
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
    */

    measureTransferRate->SchedulePulseMetrics();
}
