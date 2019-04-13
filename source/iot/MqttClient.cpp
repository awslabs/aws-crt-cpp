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

#include <aws/crt/Config.h>

namespace Aws
{
    namespace Iot
    {
        MqttClientConnectionConfig::MqttClientConnectionConfig() noexcept : m_port(0)
        {
            AWS_ZERO_STRUCT(m_socketOptions);
        }

        MqttClientConnectionConfig::MqttClientConnectionConfig(
            const Crt::String &endpoint,
            uint16_t port,
            const Crt::Io::SocketOptions &socketOptions,
            Crt::Io::TlsContext &&tlsContext)
            : m_endpoint(endpoint), m_port(port), m_context(std::move(tlsContext)), m_socketOptions(socketOptions)
        {
        }

        MqttClientConnectionConfigBuilder::MqttClientConnectionConfigBuilder(
            const char *certPath,
            const char *pkeyPath,
            Crt::Allocator *allocator) noexcept
            : m_allocator(allocator), m_portOverride(0)
        {
            m_contextOptions = Crt::Io::TlsContextOptions::InitClientWithMtls(certPath, pkeyPath, allocator);
            AWS_ZERO_STRUCT(m_socketOptions);
            m_socketOptions.connect_timeout_ms = 3000;
        }

        MqttClientConnectionConfigBuilder::MqttClientConnectionConfigBuilder(
            const Crt::ByteCursor &cert,
            const Crt::ByteCursor &pkey,
            Crt::Allocator *allocator) noexcept
            : m_allocator(allocator), m_portOverride(0)
        {
            m_contextOptions = Crt::Io::TlsContextOptions::InitClientWithMtls(cert, pkey, allocator);
            AWS_ZERO_STRUCT(m_socketOptions);
            m_socketOptions.connect_timeout_ms = 3000;
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
            m_contextOptions.OverrideDefaultTrustStore(nullptr, caPath);
            return *this;
        }

        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithCertificateAuthority(
            const Crt::ByteCursor &cert) noexcept
        {
            m_contextOptions.OverrideDefaultTrustStore(cert);
            return *this;
        }

        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithTcpKeepAlive() noexcept
        {
            m_socketOptions.keepalive = true;
            return *this;
        }
        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithTcpConnectTimeout(
            uint32_t connectTimeoutMs) noexcept
        {
            m_socketOptions.connect_timeout_ms = connectTimeoutMs;
            return *this;
        }

        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithTcpKeepAliveTimeout(
            uint16_t keepAliveTimeoutSecs) noexcept
        {
            m_socketOptions.keep_alive_timeout_sec = keepAliveTimeoutSecs;
            return *this;
        }

        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithTcpKeepAliveInterval(
            uint16_t keepAliveIntervalSecs) noexcept
        {
            m_socketOptions.keep_alive_interval_sec = keepAliveIntervalSecs;
            return *this;
        }

        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithTcpKeepAliveMaxProbes(
            uint16_t maxProbes) noexcept
        {
            m_socketOptions.keep_alive_max_failed_probes = maxProbes;
            return *this;
        }

        MqttClientConnectionConfig MqttClientConnectionConfigBuilder::Build() noexcept
        {
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
                m_contextOptions.SetAlpnList("x-amzn-mqtt-ca");
            }

            return MqttClientConnectionConfig(
                m_endpoint,
                port,
                m_socketOptions,
                Crt::Io::TlsContext(m_contextOptions, Crt::Io::TlsMode::CLIENT, m_allocator));
        }

        MqttClient::MqttClient(Crt::Io::ClientBootstrap &bootstrap, Crt::Allocator *allocator) noexcept
            : m_client(bootstrap, allocator)
        {
        }

        std::shared_ptr<Crt::Mqtt::MqttConnection> MqttClient::NewConnection(
            const MqttClientConnectionConfig &config) noexcept
        {
            auto newConnection = m_client.NewConnection(
                config.m_endpoint.c_str(),
                config.m_port,
                config.m_socketOptions,
                config.m_context.NewConnectionOptions());

            if (newConnection && newConnection->SetLogin("?SDK=CPPv2&Version=" AWS_CRT_CPP_VERSION, nullptr))
            {
                return newConnection;
            }

            return nullptr;
        }
    } // namespace Iot
} // namespace Aws