/*
 * Copyright 2010-2019 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
#include "MetricsPublisher.h"

#include <aws/crt/http/HttpConnectionManager.h>

#include <aws/common/clock.h>
#include <aws/common/task_scheduler.h>

using namespace Aws::Crt;

MetricsPublisher::MetricsPublisher(const Aws::Crt::String region,
                                   Aws::Crt::Io::TlsContext &tlsContext,
                                   Aws::Crt::Io::ClientBootstrap &clientBootstrap,
                                   Aws::Crt::Io::EventLoopGroup &elGroup,
                                   const std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> &credsProvider,
                                   const std::shared_ptr<Aws::Crt::Auth::Sigv4HttpRequestSigner> &signer,
                                   std::chrono::seconds publishFrequency) : m_signer(signer), m_credsProvider(credsProvider),
        m_elGroup(elGroup)
{
    AWS_ZERO_STRUCT(m_publishTask);
    m_publishFrequencyNs = aws_timestamp_convert(publishFrequency.count(), AWS_TIMESTAMP_SECS, AWS_TIMESTAMP_NANOS, NULL);
    m_publishTask.fn = MetricsPublisher::s_OnPublishTask;
    m_publishTask.arg = this;

    Http::HttpClientConnectionManagerOptions connectionManagerOptions;
    connectionManagerOptions.ConnectionOptions.HostName = "monitoring." + region + "amazonaws.com";
    connectionManagerOptions.ConnectionOptions.Port = 443;
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetConnectTimeoutMs(3000);
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetSocketType(AWS_SOCKET_STREAM);
    connectionManagerOptions.ConnectionOptions.InitialWindowSize = SIZE_MAX;

    connectionManagerOptions.ConnectionOptions.TlsOptions = tlsContext.NewConnectionOptions();
    auto serverName = ByteCursorFromCString(connectionManagerOptions.ConnectionOptions.HostName.c_str());
    connectionManagerOptions.ConnectionOptions.TlsOptions->SetServerName(serverName);
    connectionManagerOptions.ConnectionOptions.Bootstrap = &clientBootstrap;
    connectionManagerOptions.MaxConnections = 2;

    m_connManager = Http::HttpClientConnectionManager::NewClientConnectionManager(connectionManagerOptions, g_allocator);

    
}
