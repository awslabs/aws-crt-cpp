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

namespace
{
    const uint64_t SinglePartObjectSize = 5ULL * 1024ULL * 1024ULL * 1024ULL;

    const uint32_t MultiPartObjectNumParts = 8192;
    const uint64_t MultiPartObjectSize = (uint64_t)MultiPartObjectNumParts * (16ULL * 1024ULL * 1024ULL);
} // namespace

MeasureTransferRate::MeasureTransferRate(CanaryApp &canaryApp) : m_canaryApp(canaryApp)
{
    m_schedulingLoop = aws_event_loop_group_get_next_loop(canaryApp.GetEventLoopGroup().GetUnderlyingHandle());
}

MeasureTransferRate::~MeasureTransferRate() {}

void MeasureTransferRate::PerformMeasurement(
    const char *filenamePrefix,
    const char *keyPrefix,
    uint32_t numTransfers,
    uint32_t numConcurrentTransfers,
    uint64_t objectSize,
    uint32_t flags,
    const std::shared_ptr<S3ObjectTransport> &transport,
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

        transferFunction(i, std::move(key), objectSize, transport, std::move(notifyTransferFinished));

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

    transport->PurgeConnectionManagers();
}

void MeasureTransferRate::MeasureHttpTransfer()
{
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
        auto connOptions = m_canaryApp.GetTlsContext().NewConnectionOptions();
        connOptions.SetServerName(serverName);
        connectionManagerOptions.ConnectionOptions.TlsOptions = connOptions;
    }

    connectionManagerOptions.ConnectionOptions.Bootstrap = &m_canaryApp.GetBootstrap();
    connectionManagerOptions.MaxConnections = 5000;

    std::shared_ptr<Http::HttpClientConnectionManager> connManager =
        Http::HttpClientConnectionManager::NewClientConnectionManager(connectionManagerOptions, g_allocator);

    PerformMeasurement(
        m_canaryApp.GetOptions().downloadObjectName.c_str(),
        "httpTransferDown-",
        m_canaryApp.GetOptions().numDownTransfers,
        m_canaryApp.GetOptions().numDownConcurrentTransfers,
        SinglePartObjectSize,
        (uint32_t)MeasurementFlags::DontWarmDNSCache | (uint32_t)MeasurementFlags::NoFileSuffix,
        nullptr,
        [this, connManager, &hostHeader](
            uint32_t,
            String &&key,
            uint64_t,
            const std::shared_ptr<S3ObjectTransport> &,
            NotifyTransferFinished &&notifyTransferFinished) {
            std::shared_ptr<TransferState> transferState =
                MakeShared<TransferState>(g_allocator, m_canaryApp.GetMetricsPublisher(), 0, 1, SinglePartObjectSize);
            transferState->AddDataDownMetric(0);

            auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
            request->AddHeader(hostHeader);
            request->SetMethod(aws_http_method_get);

            StringStream keyPathStream;
            keyPathStream << "/" << key;
            String keyPath = keyPathStream.str();
            ByteCursor path = ByteCursorFromCString(keyPath.c_str());
            request->SetPath(path);

            Http::HttpRequestOptions requestOptions;
            requestOptions.request = request.get();
            requestOptions.onIncomingBody = [transferState](const Http::HttpStream &, const ByteCursor &cur) {
                transferState->AddDataDownMetric(cur.len);
            };

            requestOptions.onStreamComplete =
                [keyPath, transferState, notifyTransferFinished](Http::HttpStream &stream, int error) {
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

                    transferState->FlushDataDownMetrics();
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

    m_canaryApp.GetMetricsPublisher()->FlushMetrics();
    m_canaryApp.GetMetricsPublisher()->UploadBackup((uint32_t)UploadBackupOptions::PrintPath);
}

void MeasureTransferRate::MeasureSinglePartObjectTransfer()
{
    const char *filenamePrefix = "crt-canary-obj-small-";
    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY,
        "Measurements: %d,%d %d,%d",
        m_canaryApp.GetOptions().numUpTransfers,
        m_canaryApp.GetOptions().numUpConcurrentTransfers,
        m_canaryApp.GetOptions().numDownTransfers,
        m_canaryApp.GetOptions().numDownConcurrentTransfers);

    Vector<std::shared_ptr<TransferState>> uploads;
    Vector<std::shared_ptr<TransferState>> downloads;

    if (!m_canaryApp.GetOptions().downloadOnly)
    {
        for (uint32_t i = 0; i < m_canaryApp.GetOptions().numUpTransfers; ++i)
        {
            std::shared_ptr<TransferState> transferState =
                MakeShared<TransferState>(g_allocator, m_canaryApp.GetMetricsPublisher(), 0, 1, SinglePartObjectSize);

            uploads.push_back(transferState);
        }

        PerformMeasurement(
            "crt-canary-obj-single-part-",
            "singlePartObjectUp-",
            m_canaryApp.GetOptions().numUpTransfers,
            m_canaryApp.GetOptions().numUpConcurrentTransfers,
            SinglePartObjectSize,
            0,
            m_canaryApp.GetUploadTransport(),
            [this, &uploads](
                uint32_t transferIndex,
                String &&key,
                uint64_t,
                const std::shared_ptr<S3ObjectTransport> &transport,
                NotifyTransferFinished &&notifyTransferFinished) {
                std::shared_ptr<TransferState> transferState = uploads[transferIndex];

                transferState->AddDataUpMetric(0);

                transport->PutObject(
                    key,
                    MakeShared<MeasureTransferRateStream>(g_allocator, m_canaryApp, transferState, g_allocator),
                    0,
                    [transferState, notifyTransferFinished](int32_t errorCode, std::shared_ptr<Aws::Crt::String>) {
                        transferState->SetTransferSuccess(errorCode == AWS_ERROR_SUCCESS);
                        notifyTransferFinished(errorCode);
                    });
            });

        for (uint32_t i = 0; i < m_canaryApp.GetOptions().numUpTransfers; ++i)
        {
            uploads[i]->FlushDataUpMetrics();
        }

        m_canaryApp.GetMetricsPublisher()->FlushMetrics();
    }

    for (uint32_t i = 0; i < m_canaryApp.GetOptions().numDownTransfers; ++i)
    {
        std::shared_ptr<TransferState> transferState =
            MakeShared<TransferState>(g_allocator, m_canaryApp.GetMetricsPublisher(), 0, 1, SinglePartObjectSize);

        downloads.emplace_back(transferState);
    }

    PerformMeasurement(
        m_canaryApp.GetOptions().downloadObjectName.c_str(),
        "singlePartObjectDown-",
        m_canaryApp.GetOptions().numDownTransfers,
        m_canaryApp.GetOptions().numDownConcurrentTransfers,
        SinglePartObjectSize,
        (uint32_t)MeasurementFlags::NoFileSuffix,
        m_canaryApp.GetDownloadTransport(),
        [&downloads](
            uint32_t transferIndex,
            String &&key,
            uint64_t,
            const std::shared_ptr<S3ObjectTransport> &transport,
            NotifyTransferFinished &&notifyTransferFinished) {
            std::shared_ptr<TransferState> transferState = downloads[transferIndex];

            transferState->AddDataDownMetric(0);

            transport->GetObject(
                key,
                0,
                [transferState](const Http::HttpStream &, const ByteCursor &cur) {
                    transferState->AddDataDownMetric(cur.len);
                },
                [transferState, notifyTransferFinished](int32_t errorCode) {
                    transferState->SetTransferSuccess(errorCode == AWS_ERROR_SUCCESS);
                    notifyTransferFinished(errorCode);
                });
        });

    for (uint32_t i = 0; i < m_canaryApp.GetOptions().numDownTransfers; ++i)
    {
        downloads[i]->FlushDataDownMetrics();
    }

    m_canaryApp.GetMetricsPublisher()->FlushMetrics();
    m_canaryApp.GetMetricsPublisher()->UploadBackup((uint32_t)UploadBackupOptions::PrintPath);
}

void MeasureTransferRate::MeasureMultiPartObjectTransfer()
{
    const char *filenamePrefix = "crt-canary-obj-multipart-";

    if (!m_canaryApp.GetOptions().downloadOnly)
    {
        PerformMeasurement(
            filenamePrefix,
            "multiPartObjectUp-",
            m_canaryApp.GetOptions().numUpTransfers,
            m_canaryApp.GetOptions().numUpConcurrentTransfers,
            MultiPartObjectSize,
            0,
            m_canaryApp.GetUploadTransport(),
            [this](
                uint32_t,
                String &&key,
                uint64_t objectSize,
                const std::shared_ptr<S3ObjectTransport> &transport,
                NotifyTransferFinished &&notifyTransferFinished) {
                AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Starting upload of object %s...", key.c_str());

                transport->PutObjectMultipart(
                    key,
                    objectSize,
                    MultiPartObjectNumParts,
                    [this](const std::shared_ptr<TransferState> &transferState) {
                        return MakeShared<MeasureTransferRateStream>(
                            g_allocator, m_canaryApp, transferState, g_allocator);
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

        m_canaryApp.GetMetricsPublisher()->FlushMetrics();
    }

    PerformMeasurement(
        filenamePrefix,
        "multiPartObjectDown-",
        m_canaryApp.GetOptions().numDownTransfers,
        m_canaryApp.GetOptions().numDownConcurrentTransfers,
        MultiPartObjectSize,
        0,
        m_canaryApp.GetDownloadTransport(),
        [](uint32_t,
           String &&key,
           uint64_t,
           const std::shared_ptr<S3ObjectTransport> &transport,
           NotifyTransferFinished &&notifyTransferFinished) {
            AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Starting download of object %s...", key.c_str());

            transport->GetObjectMultipart(
                key,
                MultiPartObjectNumParts,
                [](const std::shared_ptr<TransferState> &, const ByteCursor &) {},
                [notifyTransferFinished, key](int32_t errorCode) {
                    AWS_LOGF_INFO(
                        AWS_LS_CRT_CPP_CANARY,
                        "Download finished for object %s with error code %d",
                        key.c_str(),
                        errorCode);
                    notifyTransferFinished(errorCode);
                });
        });

    m_canaryApp.GetMetricsPublisher()->FlushMetrics();
    m_canaryApp.GetMetricsPublisher()->UploadBackup((uint32_t)UploadBackupOptions::PrintPath);
}
