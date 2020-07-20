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

MeasureTransferRate::MeasureTransferRate(CanaryApp &canaryApp) : m_canaryApp(canaryApp)
{
    m_schedulingLoop = aws_event_loop_group_get_next_loop(canaryApp.GetEventLoopGroup().GetUnderlyingHandle());
}

MeasureTransferRate::~MeasureTransferRate() {}

void MeasureTransferRate::PerformMeasurement(
    const char *filenamePrefix,
    uint32_t numTransfers,
    uint32_t numConcurrentTransfers,
    uint32_t numTransfersPerAddress,
    uint32_t flags,
    const std::shared_ptr<S3ObjectTransport> &transport,
    TransferFunction &&transferFunction)
{
    if ((flags & (uint32_t)MeasurementFlags::DontWarmDNSCache) == 0)
    {
        transport->WarmDNSCache(numConcurrentTransfers, numTransfersPerAddress);
    }

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Starting performance measurement.");

    std::mutex transferCompletedMutex;
    std::condition_variable transferCompletedSignal;

    std::atomic<uint32_t> numCompleted(0);
    std::atomic<uint32_t> numInProgress(0);

    uint64_t counter = INT64_MAX - 1;

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

        transferFunction(i, std::move(key), transport, std::move(notifyTransferFinished));

        // Wait if number of in progress transfers is not less than number of concurrent transfers.
        std::unique_lock<std::mutex> guard(transferCompletedMutex);
        transferCompletedSignal.wait(guard, [&numInProgress, numConcurrentTransfers]() {
            return numInProgress.load() < numConcurrentTransfers;
        });
    }

    // Wait until all transfers have completed.
    std::unique_lock<std::mutex> guard(transferCompletedMutex);
    transferCompletedSignal.wait(
        guard, [&numCompleted, numTransfers]() { return numCompleted.load() >= numTransfers; });
}

void MeasureTransferRate::MeasureHttpTransfer()
{
    String endpoint = m_canaryApp.GetOptions().httpTestEndpoint.c_str();

    Aws::Crt::Http::HttpHeader hostHeader;
    hostHeader.name = ByteCursorFromCString("host");
    hostHeader.value = ByteCursorFromCString(endpoint.c_str());

    Http::HttpClientConnectionManagerOptions connectionManagerOptions;
    connectionManagerOptions.ConnectionOptions.HostName = endpoint;
    connectionManagerOptions.ConnectionOptions.Port = m_canaryApp.GetOptions().sendEncrypted ? 443 : 80;
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
        m_canaryApp.GetOptions().numDownTransfers,
        m_canaryApp.GetOptions().numDownConcurrentTransfers,
        0,
        (uint32_t)MeasurementFlags::DontWarmDNSCache | (uint32_t)MeasurementFlags::NoFileSuffix,
        nullptr,
        [this, connManager, &hostHeader](
            uint32_t,
            String &&key,
            const std::shared_ptr<S3ObjectTransport> &,
            NotifyTransferFinished &&notifyTransferFinished) {
            std::shared_ptr<TransferState> transferState = MakeShared<TransferState>(g_allocator);

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
                [this, keyPath, transferState, notifyTransferFinished](Http::HttpStream &stream, int error) {
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

                    transferState->SetTransferSuccess(errorCode == AWS_ERROR_SUCCESS);
                    transferState->FlushDataDownMetrics(m_canaryApp.GetMetricsPublisher());
                    notifyTransferFinished(errorCode);
                };

            connManager->AcquireConnection([transferState, requestOptions, notifyTransferFinished, request](
                                               std::shared_ptr<Http::HttpClientConnection> conn, int connErrorCode) {
                (void)request;

                if ((conn == nullptr || !conn->IsOpen()) && connErrorCode == AWS_ERROR_SUCCESS)
                {
                    connErrorCode = AWS_ERROR_UNKNOWN;
                }

                if (connErrorCode == AWS_ERROR_SUCCESS)
                {
                    transferState->InitDataDownMetric();
                    transferState->SetConnection(conn);
                    conn->NewClientStream(requestOptions)->Activate();
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
    const CanaryAppOptions &options = m_canaryApp.GetOptions();

    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY,
        "Measurements: %d,%d %d,%d",
        options.numUpTransfers,
        options.numUpConcurrentTransfers,
        options.numDownTransfers,
        options.numDownConcurrentTransfers);

    Vector<std::shared_ptr<TransferState>> uploads;
    Vector<std::shared_ptr<TransferState>> downloads;

    for (uint32_t i = 0; i < m_canaryApp.GetOptions().numUpTransfers; ++i)
    {
        std::shared_ptr<TransferState> transferState = MakeShared<TransferState>(g_allocator);

        uploads.push_back(transferState);
    }

    PerformMeasurement(
        "crt-canary-obj-single-part-",
        options.numUpTransfers,
        options.numUpConcurrentTransfers,
        options.numTransfersPerAddress,
        0,
        m_canaryApp.GetUploadTransport(),
        [this, &uploads, options](
            uint32_t transferIndex,
            String &&key,
            const std::shared_ptr<S3ObjectTransport> &transport,
            NotifyTransferFinished &&notifyTransferFinished) {
            std::shared_ptr<TransferState> transferState = uploads[transferIndex];

            transport->PutObject(
                transferState,
                key,
                MakeShared<MeasureTransferRateStream>(
                    g_allocator, m_canaryApp, transferState, options.singlePartObjectSize),
                0,
                [transferState, notifyTransferFinished](int32_t errorCode, std::shared_ptr<Aws::Crt::String>) {
                    notifyTransferFinished(errorCode);
                });
        });

    for (uint32_t i = 0; i < options.numUpTransfers; ++i)
    {
        uploads[i]->FlushDataUpMetrics(m_canaryApp.GetMetricsPublisher());
    }

    m_canaryApp.GetMetricsPublisher()->FlushMetrics();

    for (uint32_t i = 0; i < m_canaryApp.GetOptions().numDownTransfers; ++i)
    {
        std::shared_ptr<TransferState> transferState = MakeShared<TransferState>(g_allocator);

        downloads.emplace_back(transferState);
    }

    PerformMeasurement(
        m_canaryApp.GetOptions().downloadObjectName.c_str(),
        m_canaryApp.GetOptions().numDownTransfers,
        m_canaryApp.GetOptions().numDownConcurrentTransfers,
        m_canaryApp.GetOptions().numTransfersPerAddress,
        (uint32_t)MeasurementFlags::NoFileSuffix,
        m_canaryApp.GetDownloadTransport(),
        [&downloads](
            uint32_t transferIndex,
            String &&key,
            const std::shared_ptr<S3ObjectTransport> &transport,
            NotifyTransferFinished &&notifyTransferFinished) {
            std::shared_ptr<TransferState> transferState = downloads[transferIndex];

            transport->GetObject(
                transferState, key, 0, nullptr, [transferState, notifyTransferFinished](int32_t errorCode) {
                    notifyTransferFinished(errorCode);
                });
        });

    for (uint32_t i = 0; i < m_canaryApp.GetOptions().numDownTransfers; ++i)
    {
        downloads[i]->FlushDataDownMetrics(m_canaryApp.GetMetricsPublisher());
    }

    m_canaryApp.GetMetricsPublisher()->FlushMetrics();
    m_canaryApp.GetMetricsPublisher()->UploadBackup((uint32_t)UploadBackupOptions::PrintPath);
}

void MeasureTransferRate::MeasureMultiPartObjectTransfer()
{
    const char *filenamePrefix = "crt-canary-obj-multipart-";

    const CanaryAppOptions &options = m_canaryApp.GetOptions();

    Vector<std::shared_ptr<MultipartUploadState>> uploadStates;

    PerformMeasurement(
        filenamePrefix,
        options.numUpTransfers,
        options.numUpConcurrentTransfers,
        options.GetMultiPartNumTransfersPerAddress(),
        0,
        m_canaryApp.GetUploadTransport(),
        [this, &uploadStates, options](
            uint32_t,
            String &&key,
            const std::shared_ptr<S3ObjectTransport> &transport,
            NotifyTransferFinished &&notifyTransferFinished) {
            AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Starting upload of object %s...", key.c_str());

            std::shared_ptr<MultipartUploadState> uploadState = transport->PutObjectMultipart(
                key,
                options.GetMultiPartObjectSize(),
                options.multiPartObjectNumParts,
                [this, options](const std::shared_ptr<TransferState> &transferState) {
                    uint64_t partByteSize = options.multiPartObjectPartSize;

                    if (transferState->GetPartIndex() == (int32_t)options.multiPartObjectNumParts - 1)
                    {
                        partByteSize += options.GetMultiPartObjectSize() % options.multiPartObjectNumParts;
                    }

                    return MakeShared<MeasureTransferRateStream>(g_allocator, m_canaryApp, transferState, partByteSize);
                },
                [key, notifyTransferFinished](int32_t errorCode, uint32_t) {
                    AWS_LOGF_INFO(
                        AWS_LS_CRT_CPP_CANARY,
                        "Upload finished for object %s with error code %d",
                        key.c_str(),
                        errorCode);

                    notifyTransferFinished(errorCode);
                });

            uploadStates.push_back(uploadState);
        });

    for (const std::shared_ptr<MultipartUploadState> &uploadState : uploadStates)
    {
        for (const std::shared_ptr<TransferState> &part : uploadState->GetParts())
        {
            part->FlushDataUpMetrics(m_canaryApp.GetMetricsPublisher());
        }
    }

    m_canaryApp.GetMetricsPublisher()->FlushMetrics();

    Vector<std::shared_ptr<MultipartDownloadState>> downloadStates;

    PerformMeasurement(
        options.downloadObjectName.c_str(),
        options.numDownTransfers,
        options.numDownConcurrentTransfers,
        options.GetMultiPartNumTransfersPerAddress(),
        (uint32_t)MeasurementFlags::NoFileSuffix,
        m_canaryApp.GetDownloadTransport(),
        [&downloadStates, options](
            uint32_t,
            String &&key,
            const std::shared_ptr<S3ObjectTransport> &transport,
            NotifyTransferFinished &&notifyTransferFinished) {
            AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Starting download of object %s...", key.c_str());

            std::shared_ptr<MultipartDownloadState> downloadState = transport->GetObjectMultipart(
                key,
                options.multiPartObjectNumParts,
                [](const std::shared_ptr<TransferState> &, const ByteCursor &) {},
                [notifyTransferFinished, key](int32_t errorCode) {
                    AWS_LOGF_INFO(
                        AWS_LS_CRT_CPP_CANARY,
                        "Download finished for object %s with error code %d",
                        key.c_str(),
                        errorCode);
                    notifyTransferFinished(errorCode);
                });

            downloadStates.push_back(downloadState);
        });

    for (const std::shared_ptr<MultipartDownloadState> &downloadState : downloadStates)
    {
        for (const std::shared_ptr<TransferState> &part : downloadState->GetParts())
        {
            part->FlushDataDownMetrics(m_canaryApp.GetMetricsPublisher());
        }
    }

    m_canaryApp.GetMetricsPublisher()->FlushMetrics();
    m_canaryApp.GetMetricsPublisher()->UploadBackup((uint32_t)UploadBackupOptions::PrintPath);
}
