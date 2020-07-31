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
#include <algorithm>
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
    uint32_t flags,
    const std::shared_ptr<S3ObjectTransport> &transport,
    TransferFunction &&transferFunction)
{
    if ((flags & (uint32_t)MeasurementFlags::DontWarmDNSCache) == 0)
    {
        transport->WarmDNSCache(numConcurrentTransfers);
    }

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Starting performance measurement.");

    std::mutex transferCompletedMutex;
    std::condition_variable transferCompletedSignal;

    std::atomic<uint32_t> numCompleted(0);
    std::atomic<uint32_t> numInProgress(0);

    uint64_t counter = INT64_MAX - m_canaryApp.GetOptions().fileNameSuffixOffset;

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
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetSocketType(Io::SocketType::Stream);
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
                nullptr,
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
        (uint32_t)MeasurementFlags::NoFileSuffix,
        m_canaryApp.GetDownloadTransport(),
        [&downloads](
            uint32_t transferIndex,
            String &&key,
            const std::shared_ptr<S3ObjectTransport> &transport,
            NotifyTransferFinished &&notifyTransferFinished) {
            std::shared_ptr<TransferState> transferState = downloads[transferIndex];

            transport->GetObject(
                transferState, key, 0, nullptr, nullptr, [transferState, notifyTransferFinished](int32_t errorCode) {
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

void MeasureTransferRate::PerformMultipartMeasurement(
    const char *filenamePrefix,
    uint32_t numTransfers,
    uint32_t numConcurrentTransfers,
    uint32_t flags,
    const std::shared_ptr<S3ObjectTransport> &transport,
    NewMultipartStateFn &&newMultipartStateFn,
    TransferPartFn &&transferPartFn,
    EndMultipartStateFn &&endMultipartStateFn,
    PublishMetricsFn &&publishMetricsFn)
{
    if ((flags & (uint32_t)MeasurementFlags::DontWarmDNSCache) == 0)
    {
        transport->WarmDNSCache(numConcurrentTransfers);
    }

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Starting performance measurement.");

    std::mutex waitCountMutex;
    std::atomic<int32_t> waitCount(0);
    std::condition_variable waitCountSignal;

    Vector<std::shared_ptr<MultipartTransferState>> multipartTransferStates;
    Vector<std::shared_ptr<TransferLine>> transferLines;

    const CanaryAppOptions &options = m_canaryApp.GetOptions();

    // Allocate the transfer lines
    for (uint32_t i = 0; i < numConcurrentTransfers; ++i)
    {
        transferLines.push_back(
            std::make_shared<TransferLine>(i, transport, transferPartFn, waitCount, waitCountSignal));
    }

    // Create each multipart transfer state.  This includes allocation and any sync operation needed for initiatlization
    // (such as a CreateMultipartUpload request)
    for (uint32_t i = 0; i < numTransfers; ++i)
    {
        uint64_t counter = INT64_MAX - options.fileNameSuffixOffset;

        StringStream keyStream;
        keyStream << filenamePrefix;

        if ((flags & (uint32_t)MeasurementFlags::NoFileSuffix) == 0)
        {
            keyStream << counter--;
        }

        String key = keyStream.str();

        if (counter == 0)
        {
            counter = INT64_MAX;
        }

        newMultipartStateFn(
            key,
            options.GetMultiPartObjectSize(),
            options.multiPartObjectNumParts,
            [&multipartTransferStates, &waitCount, &waitCountSignal](
                std::shared_ptr<MultipartTransferState> multipartTransferState) {
                multipartTransferStates.push_back(multipartTransferState);
                waitCount.fetch_add(1);
                waitCountSignal.notify_one();
            });
    }

    // Wait for all transfers to be initialized.
    {
        std::unique_lock<std::mutex> guard(waitCountMutex);
        waitCountSignal.wait(
            guard, [&waitCount, numTransfers]() { return (uint32_t)waitCount.load() >= numTransfers; });
        waitCount.exchange(0);
    }

    uint32_t lineIndex = 0;

    // Assign each multipart transfer state to n number of lines where n = number of parts for that multipart transfer
    for (uint32_t i = 0; i < numTransfers; ++i)
    {
        std::shared_ptr<MultipartTransferState> multipartTransferState = multipartTransferStates[i];

        for (int32_t j = 0; j < (int32_t)options.multiPartObjectNumParts; ++j)
        {
            Vector<std::shared_ptr<MultipartTransferState>> &transferLineStates =
                transferLines[lineIndex]->m_multipartTransferStates;
            auto it = std::find(transferLineStates.begin(), transferLineStates.end(), multipartTransferState);

            // If this line doesn't already know about this transfer, add it to the line.
            if (it == transferLineStates.end())
            {
                transferLineStates.emplace_back(multipartTransferState);
            }

            lineIndex = (lineIndex + 1) % numConcurrentTransfers;
        }
    }

    // Start transferring data
    for (uint32_t i = 0; i < numConcurrentTransfers; ++i)
    {
        ProcessTransferLinePart(transferLines[i]);
    }

    // Wait until all transfers have completed.
    {
        std::unique_lock<std::mutex> guard(waitCountMutex);
        waitCountSignal.wait(
            guard, [&waitCount, numTransfers]() { return (uint32_t)waitCount.load() >= numTransfers; });
        waitCount.exchange(0);
    }

    transferLines.clear();

    // Kick off ending all transfers now.  This will allow for things like CompleteMultipartUpload to happen.
    for (uint32_t i = 0; i < numTransfers; ++i)
    {
        std::shared_ptr<MultipartTransferState> multipartTransferState = multipartTransferStates[i];

        endMultipartStateFn(
            multipartTransferState,
            [&waitCount, &waitCountSignal](std::shared_ptr<MultipartTransferState> multipartTransferState) {
                waitCount.fetch_add(1);
                waitCountSignal.notify_one();
            });
    }

    // Wait until transfers have said they have completed.
    {
        std::unique_lock<std::mutex> guard(waitCountMutex);
        waitCountSignal.wait(
            guard, [&waitCount, numTransfers]() { return (uint32_t)waitCount.load() >= numTransfers; });
        waitCount.exchange(0);
    }

    // Publish metrics for those transfers!
    publishMetricsFn(multipartTransferStates);
}

void MeasureTransferRate::ProcessTransferLinePart(std::shared_ptr<TransferLine> transferLine)
{
    std::shared_ptr<MultipartTransferState> multipartTransferState;
    std::shared_ptr<TransferState> partTransferState;

    Vector<std::shared_ptr<MultipartTransferState>> &multipartTransferStates = transferLine->m_multipartTransferStates;

    // Find next unfinished state
    for (int32_t i = 0; i < (int32_t)multipartTransferStates.size(); ++i)
    {
        int &index = transferLine->m_currentIndex;

        index = (index + i) % multipartTransferStates.size();

        partTransferState = multipartTransferStates[index]->PopNextPart();

        if (partTransferState != nullptr)
        {
            multipartTransferState = multipartTransferStates[index];
            break;
        }
    }

    // If we couldn't find an unfinished multipart state, then we are done.
    if (multipartTransferState == nullptr)
    {
        transferLine->m_waitCount.fetch_add(1);
        transferLine->m_waitCountSignal.notify_one();
        return;
    }

    partTransferState->SetConnection(transferLine->m_connection);

    NotifyTransferFinished notifyPartFinished = [this, multipartTransferState, partTransferState, transferLine](
                                                    int32_t errorCode) {
        const String &key = multipartTransferState->GetKey();

        if (errorCode != AWS_ERROR_SUCCESS)
        {
            AWS_LOGF_ERROR(
                AWS_LS_CRT_CPP_CANARY,
                "Did not receive part #%d for %s",
                partTransferState->GetPartNumber(),
                key.c_str());

            multipartTransferState->RequeuePart(partTransferState);
        }
        else
        {
            AWS_LOGF_INFO(
                AWS_LS_CRT_CPP_CANARY, "Transferred part #%d for %s", partTransferState->GetPartNumber(), key.c_str());

            if (multipartTransferState->IncNumPartsCompleted())
            {
                AWS_LOGF_DEBUG(AWS_LS_CRT_CPP_CANARY, "Finished trying to transfer all parts for %s", key.c_str());
            }
        }

        transferLine->m_connection = partTransferState->GetConnection();

        AWS_LOGF_INFO(
            AWS_LS_CRT_CPP_CANARY,
            "Transfer line index %d used connection %p for multipart transfer %p",
            transferLine->m_transferLineIndex,
            (void *)partTransferState->GetConnection().get(),
            (void *)transferLine->m_multipartTransferStates[transferLine->m_currentIndex].get());

        partTransferState->SetConnection(nullptr);

        ProcessTransferLinePart(transferLine);
    };

    transferLine->m_transferPartFn(
        transferLine, multipartTransferState, partTransferState, std::move(notifyPartFinished));
}

void MeasureTransferRate::MeasureMultiPartObjectTransfer()
{
    const char *filenamePrefix = "crt-canary-obj-multipart-";
    const CanaryAppOptions &options = m_canaryApp.GetOptions();
    std::shared_ptr<S3ObjectTransport> uploadTransport = m_canaryApp.GetUploadTransport();

    PerformMultipartMeasurement(
        filenamePrefix,
        options.numUpTransfers,
        options.numUpConcurrentTransfers,
        0,
        uploadTransport,
        [uploadTransport](
            const Aws::Crt::String &key,
            size_t objectSize,
            int32_t numParts,
            GenericMultipartStateCallback &&callback) {
            std::shared_ptr<MultipartUploadState> multipartUploadState =
                std::make_shared<MultipartUploadState>(key, objectSize, numParts);

            uploadTransport->CreateMultipartUpload(
                multipartUploadState->GetKey(),
                [multipartUploadState, callback](int errorCode, const Aws::Crt::String &uploadId) {
                    if (errorCode != AWS_ERROR_SUCCESS)
                    {
                        multipartUploadState->SetFinished(errorCode);
                        return;
                    }

                    multipartUploadState->SetUploadId(uploadId);

                    callback(multipartUploadState);
                });
        },
        [this, options](
            std::shared_ptr<TransferLine> transferLine,
            std::shared_ptr<MultipartTransferState> multipartTransferState,
            std::shared_ptr<TransferState> partTransferState,
            NotifyTransferFinished &&notifyTransferFinished) {
            std::shared_ptr<MultipartUploadState> multipartUploadState =
                std::static_pointer_cast<MultipartUploadState>(multipartTransferState);

            StringStream keyPathStream;
            keyPathStream << multipartTransferState->GetKey() << "?partNumber=" << partTransferState->GetPartNumber()
                          << "&uploadId=" << multipartUploadState->GetUploadId();

            String keyPathStr = keyPathStream.str();

            uint64_t partByteSize = options.multiPartObjectPartSize;

            if (partTransferState->GetPartIndex() == (int32_t)options.multiPartObjectNumParts - 1)
            {
                partByteSize += options.GetMultiPartObjectSize() % options.multiPartObjectNumParts;
            }

            std::shared_ptr<MeasureTransferRateStream> inputStream =
                MakeShared<MeasureTransferRateStream>(g_allocator, m_canaryApp, partTransferState, partByteSize);

            transferLine->m_transport->PutObject(
                partTransferState,
                keyPathStr,
                inputStream,
                (uint32_t)EPutObjectFlags::RetrieveETag,
                [multipartTransferState,
                 partTransferState](std::shared_ptr<Http::HttpClientConnection> connection, int32_t errorCode) {
                    if (errorCode != AWS_ERROR_SUCCESS)
                    {
                        connection = nullptr;
                    }

                    partTransferState->SetConnection(connection);
                },
                [multipartTransferState, partTransferState, notifyTransferFinished](
                    int32_t errorCode, std::shared_ptr<Aws::Crt::String> etag) {
                    std::shared_ptr<MultipartUploadState> multipartUploadState =
                        std::static_pointer_cast<MultipartUploadState>(multipartTransferState);

                    if (etag == nullptr)
                    {
                        errorCode = AWS_ERROR_UNKNOWN;
                    }

                    if (errorCode == AWS_ERROR_SUCCESS)
                    {
                        multipartUploadState->SetETag(partTransferState->GetPartIndex(), *etag);
                    }

                    notifyTransferFinished(errorCode);
                });
        },
        [uploadTransport](
            std::shared_ptr<MultipartTransferState> multipartTransferState, GenericMultipartStateCallback callback) {
            std::shared_ptr<MultipartUploadState> multipartUploadState =
                std::static_pointer_cast<MultipartUploadState>(multipartTransferState);

            // multipartUploadState->SetConnection(nullptr);

            Aws::Crt::Vector<Aws::Crt::String> etags;
            multipartUploadState->GetETags(etags);

            uploadTransport->CompleteMultipartUpload(
                multipartUploadState->GetKey(),
                multipartUploadState->GetUploadId(),
                etags,
                [multipartUploadState, callback](int32_t errorCode) {
                    multipartUploadState->SetFinished(errorCode);

                    callback(multipartUploadState);
                });
        },
        [this](Vector<std::shared_ptr<MultipartTransferState>> &multipartTransferStates) {
            (void)multipartTransferStates;

            for (const std::shared_ptr<MultipartTransferState> &transferState : multipartTransferStates)
            {
                for (const std::shared_ptr<TransferState> &part : transferState->GetParts())
                {
                    part->FlushDataUpMetrics(m_canaryApp.GetMetricsPublisher());
                }
            }

            m_canaryApp.GetMetricsPublisher()->FlushMetrics();
        });

    PerformMultipartMeasurement(
        options.downloadObjectName.c_str(),
        options.numDownTransfers,
        options.numDownConcurrentTransfers,
        (uint32_t)MeasurementFlags::NoFileSuffix,
        m_canaryApp.GetDownloadTransport(),
        [](const Aws::Crt::String &key, size_t objectSize, int32_t numParts, GenericMultipartStateCallback &&callback) {
            std::shared_ptr<MultipartDownloadState> multipartDownloadState =
                std::make_shared<MultipartDownloadState>(key, objectSize, numParts);

            callback(multipartDownloadState);
        },
        [](std::shared_ptr<TransferLine> transferLine,
           std::shared_ptr<MultipartTransferState> multipartTransferState,
           std::shared_ptr<TransferState> partTransferState,
           NotifyTransferFinished &&notifyTransferFinished) {
            transferLine->m_transport->GetObject(
                partTransferState,
                multipartTransferState->GetKey(),
                partTransferState->GetPartNumber(),
                [](Http::HttpStream &stream, const ByteCursor &data) {
                    (void)stream;
                    (void)data;
                    // AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "%s", (const char*)data.ptr);
                },
                [multipartTransferState,
                 partTransferState](std::shared_ptr<Http::HttpClientConnection> connection, int32_t errorCode) {
                    if (errorCode != AWS_ERROR_SUCCESS)
                    {
                        connection = nullptr;
                    }

                    partTransferState->SetConnection(connection);
                },
                [notifyTransferFinished](int32_t errorCode) { notifyTransferFinished(errorCode); });
        },
        [](std::shared_ptr<MultipartTransferState> multipartTransferState, GenericMultipartStateCallback callback) {
            std::shared_ptr<MultipartDownloadState> multipartDownloadState =
                std::static_pointer_cast<MultipartDownloadState>(multipartTransferState);

            // multipartDownloadState->SetConnection(nullptr);
            callback(multipartDownloadState);
        },
        [this](Vector<std::shared_ptr<MultipartTransferState>> &multipartTransferStates) {
            for (const std::shared_ptr<MultipartTransferState> &transferState : multipartTransferStates)
            {
                for (const std::shared_ptr<TransferState> &part : transferState->GetParts())
                {
                    part->FlushDataDownMetrics(m_canaryApp.GetMetricsPublisher());
                }
            }

            m_canaryApp.GetMetricsPublisher()->FlushMetrics();
            m_canaryApp.GetMetricsPublisher()->UploadBackup((uint32_t)UploadBackupOptions::PrintPath);
        });
}
