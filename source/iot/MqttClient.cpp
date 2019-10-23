/*
 * Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
#include <aws/iot/MqttClient.h>

#include <aws/crt/Api.h>
#include <aws/crt/Config.h>

namespace Aws
{
    namespace Iot
    {
        MqttClientConnectionConfig::MqttClientConnectionConfig(int lastError) noexcept
            : m_port(0), m_lastError(lastError)
        {
        }

        MqttClientConnectionConfig MqttClientConnectionConfig::CreateInvalid(int lastError) noexcept
        {
            return MqttClientConnectionConfig(lastError);
        }

        MqttClientConnectionConfig::MqttClientConnectionConfig(
            const Crt::String &endpoint,
            uint16_t port,
            const Crt::Io::SocketOptions &socketOptions,
            Crt::Io::TlsContext &&tlsContext)
            : m_endpoint(endpoint), m_port(port), m_context(std::move(tlsContext)), m_socketOptions(socketOptions)
        {
        }

        MqttClientConnectionConfig::MqttClientConnectionConfig(
            const Crt::String &endpoint,
            uint16_t port,
            const Crt::Io::SocketOptions &socketOptions,
            Crt::Io::TlsContext &&tlsContext,
            Crt::Mqtt::OnWebSocketHandshakeIntercept &&interceptor)
            : m_endpoint(endpoint), m_port(port), m_context(std::move(tlsContext)), m_socketOptions(socketOptions),
              m_webSocketInterceptor(std::move(interceptor))
        {
        }

        MqttClientConnectionConfigBuilder::MqttClientConnectionConfigBuilder(
            const char *certPath,
            const char *pkeyPath,
            Crt::Allocator *allocator) noexcept
            : m_allocator(allocator), m_portOverride(0), m_lastError(0)
        {
            m_socketOptions.SetConnectTimeoutMs(3000);
            m_contextOptions = Crt::Io::TlsContextOptions::InitClientWithMtls(certPath, pkeyPath, allocator);
            if (!m_contextOptions)
            {
                m_lastError = Aws::Crt::LastErrorOrUnknown();
            }
        }

        MqttClientConnectionConfigBuilder::MqttClientConnectionConfigBuilder(
            const Crt::ByteCursor &cert,
            const Crt::ByteCursor &pkey,
            Crt::Allocator *allocator) noexcept
            : m_allocator(allocator), m_portOverride(0), m_lastError(0)
        {
            m_socketOptions.SetConnectTimeoutMs(3000);
            m_contextOptions = Crt::Io::TlsContextOptions::InitClientWithMtls(cert, pkey, allocator);
            if (!m_contextOptions)
            {
                m_lastError = Aws::Crt::LastErrorOrUnknown();
            }
        }

        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithEndpoint(const Crt::String &endpoint)
        {
            m_endpoint = endpoint;
            return *this;
        }

        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithEndpoint(Crt::String &&endpoint)
        {
            m_endpoint = std::move(endpoint);
            return *this;
        }

        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithPortOverride(uint16_t port) noexcept
        {
            m_portOverride = port;
            return *this;
        }

        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithCertificateAuthority(
            const char *caPath) noexcept
        {
            if (m_contextOptions)
            {
                if (!m_contextOptions.OverrideDefaultTrustStore(nullptr, caPath))
                {
                    m_lastError = Aws::Crt::LastErrorOrUnknown();
                }
            }
            return *this;
        }

        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithCertificateAuthority(
            const Crt::ByteCursor &cert) noexcept
        {
            if (m_contextOptions)
            {
                if (!m_contextOptions.OverrideDefaultTrustStore(cert))
                {
                    m_lastError = Aws::Crt::LastErrorOrUnknown();
                }
            }
            return *this;
        }

        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithTcpKeepAlive() noexcept
        {
            m_socketOptions.SetKeepAlive(true);
            return *this;
        }
        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithTcpConnectTimeout(
            uint32_t connectTimeoutMs) noexcept
        {
            m_socketOptions.SetConnectTimeoutMs(connectTimeoutMs);
            return *this;
        }

        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithTcpKeepAliveTimeout(
            uint16_t keepAliveTimeoutSecs) noexcept
        {
            m_socketOptions.SetKeepAliveTimeoutSec(keepAliveTimeoutSecs);
            return *this;
        }

        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithTcpKeepAliveInterval(
            uint16_t keepAliveIntervalSecs) noexcept
        {
            m_socketOptions.SetKeepAliveIntervalSec(keepAliveIntervalSecs);
            return *this;
        }

        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithTcpKeepAliveMaxProbes(
            uint16_t maxProbes) noexcept
        {
            m_socketOptions.SetKeepAliveMaxFailedProbes(maxProbes);
            return *this;
        }

        MqttClientConnectionConfig MqttClientConnectionConfigBuilder::Build() noexcept
        {
            if (m_lastError)
            {
                return MqttClientConnectionConfig::CreateInvalid(m_lastError);
            }

            uint16_t port = m_portOverride;

            if (!m_portOverride)
            {
                port = 8883;

                if (Crt::Io::TlsContextOptions::IsAlpnSupported())
                {
                    port = 443;
                }
            }

            if (port == 443 && Crt::Io::TlsContextOptions::IsAlpnSupported())
            {
                if (!m_contextOptions.SetAlpnList("x-amzn-mqtt-ca"))
                {
                    return MqttClientConnectionConfig::CreateInvalid(Aws::Crt::LastErrorOrUnknown());
                }
            }

            return MqttClientConnectionConfig(
                m_endpoint,
                port,
                m_socketOptions,
                Crt::Io::TlsContext(m_contextOptions, Crt::Io::TlsMode::CLIENT, m_allocator));
        }

        MqttClient::MqttClient(Crt::Io::ClientBootstrap &bootstrap, Crt::Allocator *allocator) noexcept
            : m_client(bootstrap, allocator), m_lastError(0)
        {
            if (!m_client)
            {
                m_lastError = m_client.LastError();
            }
        }

        std::shared_ptr<Crt::Mqtt::MqttConnection> MqttClient::NewConnection(
            const MqttClientConnectionConfig &config) noexcept
        {
            if (!config)
            {
                m_lastError = config.LastError();
                return nullptr;
            }

            bool useWebsocket = config.m_webSocketInterceptor.operator bool();
            auto newConnection = m_client.NewConnection(
                config.m_endpoint.c_str(), config.m_port, config.m_socketOptions, config.m_context, useWebsocket);

            if (!newConnection)
            {
                m_lastError = m_client.LastError();
                return nullptr;
            }

            if (!(*newConnection) || !newConnection->SetLogin("?SDK=CPPv2&Version=" AWS_CRT_CPP_VERSION, nullptr))
            {
                m_lastError = newConnection->LastError();
                return nullptr;
            }

            if (useWebsocket)
            {
                newConnection->WebsocketInterceptor = config.m_webSocketInterceptor;
            }

            return newConnection;
        }
    } // namespace Iot
} // namespace Aws