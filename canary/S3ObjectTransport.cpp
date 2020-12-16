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

    struct aws_signing_config_aws signing_config;
    aws_s3_init_default_signing_config(
        &signing_config,
        aws_byte_cursor_from_c_str(m_canaryApp.GetOptions().region.c_str()),
        m_canaryApp.GetCredsProvider()->GetUnderlyingHandle());

    struct aws_s3_client_config client_config;
    AWS_ZERO_STRUCT(client_config);
    client_config.client_bootstrap = m_canaryApp.GetBootstrap().GetUnderlyingHandle();
    client_config.signing_config = &signing_config;
    client_config.shutdown_callback = CanaryApp::ShutdownCallbackDecRefCount;
    client_config.shutdown_callback_user_data = NULL;
    client_config.throughput_target_gbps = canaryApp.GetOptions().targetThroughputGbps;

    CanaryApp::IncResourceRefCount();
    m_client = aws_s3_client_new(g_allocator, &client_config);

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

S3ObjectTransport::~S3ObjectTransport()
{
    aws_s3_client_release(m_client);
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

void S3ObjectTransport::PutObject(const std::shared_ptr<TransferState> &transferState, const Aws::Crt::String &key)
{
    AWS_ASSERT(transferState->GetBody() != nullptr);

    auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);

    AddContentLengthHeader(request, transferState->GetBody());

    request->AddHeader(m_hostHeader);
    request->AddHeader(m_contentTypeHeader);
    request->SetBody(transferState->GetBody());
    request->SetMethod(aws_http_method_put);

    StringStream keyPathStream;
    keyPathStream << "/" << key;

    String keyPath = keyPathStream.str();
    ByteCursor path = ByteCursorFromCString(keyPath.c_str());
    request->SetPath(path);

    SendMetaRequest(transferState, AWS_S3_META_REQUEST_TYPE_PUT_OBJECT, request->GetUnderlyingMessage());

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "PutObject initiated for path %s...", keyPath.c_str());
}

void S3ObjectTransport::GetObject(const std::shared_ptr<TransferState> &transferState, const Aws::Crt::String &key)
{
    auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
    request->AddHeader(m_hostHeader);
    request->SetMethod(aws_http_method_get);

    StringStream keyPathStream;
    keyPathStream << "/" << key;

    String keyPath = keyPathStream.str();
    ByteCursor path = ByteCursorFromCString(keyPath.c_str());
    request->SetPath(path);

    SendMetaRequest(transferState, AWS_S3_META_REQUEST_TYPE_GET_OBJECT, request->GetUnderlyingMessage());

    AWS_LOGF_INFO(AWS_LS_CRT_CPP_CANARY, "GetObject initiated for path %s...", keyPath.c_str());
}

void S3ObjectTransport::SendMetaRequest(
    const std::shared_ptr<TransferState> &transferState,
    enum aws_s3_meta_request_type meta_request_type,
    struct aws_http_message *message)
{
    aws_s3_meta_request_options meta_request_options;
    AWS_ZERO_STRUCT(meta_request_options);
    meta_request_options.headers_callback = TransferState::StaticIncomingHeaders;
    meta_request_options.body_callback = TransferState::StaticIncomingBody;
    meta_request_options.finish_callback = TransferState::StaticFinish;
    meta_request_options.shutdown_callback = CanaryApp::ShutdownCallbackDecRefCount;
    meta_request_options.user_data = transferState.get();
    meta_request_options.type = meta_request_type;
    meta_request_options.message = message;

    aws_s3_meta_request *meta_request = aws_s3_client_make_meta_request(m_client, &meta_request_options);
    AWS_ASSERT(meta_request != nullptr);

    CanaryApp::IncResourceRefCount();
    transferState->SetMetaRequest(meta_request);
}
