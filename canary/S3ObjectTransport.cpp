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
#include "S3ObjectTransport.h"
#include "CanaryApp.h"
#include "MetricsPublisher.h"

#include <aws/common/thread.h>
#include <aws/crt/Api.h>
#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/Stream.h>
#include <aws/io/stream.h>
#include <inttypes.h>
#include <iostream>

#if defined(_WIN32)
#    undef min
#endif

using namespace Aws::Crt;

namespace
{
    const int32_t S3GetObjectResponseStatus_PartialContent = 206;
} // namespace

S3ObjectTransport::S3ObjectTransport(
    CanaryApp &canaryApp,
    const Aws::Crt::String &bucket,
    uint32_t maxConnections,
    uint64_t minThroughputBytesPerSecond)
    : m_canaryApp(canaryApp), m_bucketName(bucket)
{
    m_endpoint = m_bucketName + ".s3." + m_canaryApp.GetOptions().region.c_str() + ".amazonaws.com";

    m_hostHeader.name = ByteCursorFromCString("host");
    m_hostHeader.value = ByteCursorFromCString(m_endpoint.c_str());

    m_contentTypeHeader.name = ByteCursorFromCString("content-type");
    m_contentTypeHeader.value = ByteCursorFromCString("text/plain");

    /*m_minThroughputBytes = minThroughputBytesPerSecond;

    const CanaryAppOptions &options = m_canaryApp.GetOptions();

    if (options.sendEncrypted)
    {
        aws_byte_cursor serverName = ByteCursorFromCString(m_endpoint.c_str());
        auto connOptions = m_canaryApp.GetTlsContext().NewConnectionOptions();
        connOptions.SetServerName(serverName);
    }

    if (m_minThroughputBytes > 0)
    {

        if (options.endPointMonitoringEnabled)
        {
            AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Endpoint monitoring enabled.");

            Io::EndPointMonitorOptions options;
            options.m_expectedPerSampleThroughput = m_minThroughputBytes;
            options.m_allowedFailureInterval = 2ULL;
            options.m_schedulingLoop =
                aws_event_loop_group_get_next_loop(canaryApp.GetEventLoopGroup().GetUnderlyingHandle());
            options.m_hostResolver = canaryApp.GetDefaultHostResolver().GetUnderlyingHandle();
            options.m_endPoint = m_endpoint;

            m_endPointMonitorManager =
                MakeShared<Io::EndPointMonitorManager>(g_allocator, options); // TODO use unique pointer

            connectionManagerOptions.OnConnectionCreated = [this](struct aws_http_connection *connection) {
                if (m_endPointMonitorManager != nullptr)
                {
                    m_endPointMonitorManager->AttachMonitor(connection);
                }
            };
        }

        if (options.connectionMonitoringEnabled)
        {
            AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Connection monitoring enabled.");

            Http::HttpConnectionMonitoringOptions monitoringOptions;
            monitoringOptions.allowable_throughput_failure_interval_seconds =
                options.connectionMonitoringFailureIntervalSeconds;
            monitoringOptions.minimum_throughput_bytes_per_second = m_minThroughputBytes;

            connectionManagerOptions.ConnectionOptions.MonitoringOptions = monitoringOptions;
        }
    }*/
}

void S3ObjectTransport::WarmDNSCache(uint32_t numTransfers)
{
    /*
        if (m_endPointMonitorManager != nullptr)
        {
            m_endPointMonitorManager->SetupCallbacks();
        }
    */
    uint32_t transfersPerAddress = m_canaryApp.GetOptions().numTransfersPerAddress;

    // Each transfer is in a group the size of TransfersPerAddress,
    size_t desiredNumberOfAddresses = numTransfers / static_cast<size_t>(transfersPerAddress);

    if ((numTransfers % transfersPerAddress) > 0)
    {
        ++desiredNumberOfAddresses;
    }

    AWS_LOGF_INFO(
        AWS_LS_CRT_CPP_CANARY,
        "Warming DNS cache: getting %" PRIu64 " addresses for endpoint %s",
        (uint64_t)desiredNumberOfAddresses,
        m_endpoint.c_str());

    aws_host_resolver_purge_cache(m_canaryApp.GetDefaultHostResolver().GetUnderlyingHandle());

    // Ask the host resolver to start resolving.
    m_canaryApp.GetDefaultHostResolver().ResolveHost(
        m_endpoint, [](Io::HostResolver &, const Vector<Io::HostAddress> &, int) {});

    // Wait until the resolved address count is what we need it to be.
    {
        size_t numAddresses = m_canaryApp.GetDefaultHostResolver().GetHostAddressCount(
            m_endpoint, AWS_GET_HOST_ADDRESS_COUNT_RECORD_TYPE_A);

        while (numAddresses < desiredNumberOfAddresses)
        {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            numAddresses = m_canaryApp.GetDefaultHostResolver().GetHostAddressCount(
                m_endpoint, AWS_GET_HOST_ADDRESS_COUNT_RECORD_TYPE_A);
        }
    }

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "DNS cache warmed.");
}

void S3ObjectTransport::AddContentLengthHeader(
    const std::shared_ptr<Http::HttpRequest> &request,
    const std::shared_ptr<Aws::Crt::Io::InputStream> &body)
{
    Http::HttpHeader contentLength;
    contentLength.name = ByteCursorFromCString("content-length");

    StringStream intValue;
    intValue << body->GetLength();
    String contentLengthVal = intValue.str();
    contentLength.value = ByteCursorFromCString(contentLengthVal.c_str());
    request->AddHeader(contentLength);
}

void S3ObjectTransport::PutObject(
    const std::shared_ptr<TransferState> &transferState,
    const Aws::Crt::String &key,
    const std::shared_ptr<Io::InputStream> &body,
    uint32_t flags,
    const PutObjectFinished &finishedCallback)
{
    AWS_FATAL_ASSERT(body.get() != nullptr);

    auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);

    AddContentLengthHeader(request, body);

    request->AddHeader(m_hostHeader);
    request->AddHeader(m_contentTypeHeader);
    request->SetBody(body);
    request->SetMethod(aws_http_method_put);

    StringStream keyPathStream;
    keyPathStream << "/" << key;

    String keyPath = keyPathStream.str();
    ByteCursor path = ByteCursorFromCString(keyPath.c_str());
    request->SetPath(path);

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "PutObject initiated for path %s...", keyPath.c_str());

    std::shared_ptr<String> etag = nullptr;

    if ((flags & (uint32_t)EPutObjectFlags::RetrieveETag) != 0)
    {
        etag = MakeShared<String>(g_allocator);
    }

    Http::HttpRequestOptions requestOptions;
    requestOptions.request = nullptr;

    /* TODO Header callback.  Ignore ETag*/
    requestOptions.onIncomingHeaders = [etag, transferState](
                                           Http::HttpStream &stream,
                                           enum aws_http_header_block headerBlock,
                                           const Http::HttpHeader *headersArray,
                                           std::size_t headersCount) {
        (void)stream;
        (void)headerBlock;

        if (transferState != nullptr)
        {
            transferState->ProcessHeaders(headersArray, headersCount);
        }

        if (etag == nullptr)
        {
            return;
        }

        for (size_t i = 0; i < headersCount; ++i)
        {
            const aws_byte_cursor &name = headersArray[i].name;

            if (aws_byte_cursor_eq_c_str(&name, "ETag"))
            {
                const aws_byte_cursor &value = headersArray[i].value;
                *etag = String((const char *)value.ptr, value.len);
            }
        }
    };

    /* TODO Finish callback.  Ignore ETags. */
    requestOptions.onStreamComplete =
        [transferState, keyPath, etag, finishedCallback](Http::HttpStream &stream, int errorCode) {
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
                    "PutObject finished for path %s with response status %d",
                    keyPath.c_str(),
                    stream.GetResponseStatusCode());
            }
            else
            {
                AWS_LOGF_DEBUG(
                    AWS_LS_CRT_CPP_CANARY,
                    "PutObject finished for path %s with error '%s'",
                    keyPath.c_str(),
                    aws_error_debug_str(errorCode));
            }

            if (transferState != nullptr)
            {
                transferState->SetTransferSuccess(errorCode == AWS_ERROR_SUCCESS);
            }

            finishedCallback(errorCode, etag);
        };

    /*
        std::shared_ptr<Http::HttpClientConnection> existingConn =
            transferState != nullptr ? transferState->GetConnection() : nullptr;

        MakeSignedRequest(
            existingConn,
            request,
            requestOptions,
            [keyPath, transferState, connectionCallback, finishedCallback](
                std::shared_ptr<Http::HttpClientConnection> conn, int32_t errorCode) {
                if (connectionCallback)
                {
                    connectionCallback(conn, errorCode);
                }

                if (errorCode != AWS_ERROR_SUCCESS)
                {
                    AWS_LOGF_ERROR(AWS_LS_CRT_CPP_CANARY, "Making signed request failed with error code %d", errorCode);
                    finishedCallback(errorCode, nullptr);
                }
                else
                {
                    if (transferState != nullptr)
                    {
                        transferState->SetConnection(conn);
                    }
                }
            });
        */
}

void S3ObjectTransport::GetObject(
    const std::shared_ptr<TransferState> &transferState,
    const Aws::Crt::String &key,
    uint32_t partNumber,
    Aws::Crt::Http::OnIncomingBody onIncomingBody,
    const GetObjectFinished &getObjectFinished)
{
    auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
    request->AddHeader(m_hostHeader);

    request->SetMethod(aws_http_method_get);

    StringStream keyPathStream;
    keyPathStream << "/" << key;

    if (partNumber > 0)
    {
        keyPathStream << "?partNumber=" << partNumber;
    }

    String keyPath = keyPathStream.str();
    ByteCursor path = ByteCursorFromCString(keyPath.c_str());
    request->SetPath(path);

    Http::HttpRequestOptions requestOptions;
    requestOptions.request = nullptr;

    /* TODO Body Callback. InitDataDown metric here too. */
    requestOptions.onIncomingBody = [transferState, onIncomingBody](Http::HttpStream &stream, const ByteCursor &cur) {
        if (transferState != nullptr)
        {
            transferState->AddDataDownMetric(cur.len);
        }

        if (onIncomingBody != nullptr)
        {
            onIncomingBody(stream, cur);
        }
    };

    /* TODO Header callback. */
    requestOptions.onIncomingHeaders = [transferState](
                                           Http::HttpStream &stream,
                                           enum aws_http_header_block headerBlock,
                                           const Http::HttpHeader *headersArray,
                                           std::size_t headersCount) {
        if (transferState != nullptr)
        {
            transferState->ProcessHeaders(headersArray, headersCount);
        }
    };

    /* TODO Finish callback. */
    requestOptions.onStreamComplete =
        [keyPath, partNumber, transferState, getObjectFinished](Http::HttpStream &stream, int error) {
            int errorCode = error;

            if (errorCode == AWS_ERROR_SUCCESS)
            {
                int32_t successStatus = partNumber > 0 ? S3GetObjectResponseStatus_PartialContent : 200;

                if (stream.GetResponseStatusCode() != successStatus)
                {
                    errorCode = AWS_ERROR_UNKNOWN;
                }

                aws_log_level logLevel = (errorCode != AWS_ERROR_SUCCESS) ? AWS_LL_ERROR : AWS_LL_INFO;

                AWS_LOGF(
                    logLevel,
                    AWS_LS_CRT_CPP_CANARY,
                    "GetObject finished for path %s with response status %d",
                    keyPath.c_str(),
                    stream.GetResponseStatusCode());
            }
            else
            {
                AWS_LOGF_ERROR(
                    AWS_LS_CRT_CPP_CANARY,
                    "GetObject finished for path %s with error '%s'",
                    keyPath.c_str(),
                    aws_error_debug_str(errorCode));
            }

            if (transferState != nullptr)
            {
                transferState->SetTransferSuccess(errorCode == AWS_ERROR_SUCCESS);
            }

            getObjectFinished(errorCode);
        };
    /*
        std::shared_ptr<Http::HttpClientConnection> existingConn =
            transferState != nullptr ? transferState->GetConnection() : nullptr;

        MakeSignedRequest(
            existingConn,
            request,
            requestOptions,
            [transferState, connectionCallback, getObjectFinished](
                std::shared_ptr<Http::HttpClientConnection> conn, int32_t errorCode) {
                if (connectionCallback)
                {
                    connectionCallback(conn, errorCode);
                }

                if (errorCode != AWS_ERROR_SUCCESS)
                {
                    getObjectFinished(errorCode);
                }
                else
                {
                    // AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "Setting transfer state address to %s", connAddr.c_str());
                    if (transferState != nullptr)
                    {
                        transferState->InitDataDownMetric();
                    }

                    if (transferState != nullptr)
                    {
                        transferState->SetConnection(conn);
                    }
                }
            });
            */
}
