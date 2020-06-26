/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/iot/MqttClient.h>

#include <aws/crt/Api.h>
#include <aws/crt/Config.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/auth/Sigv4Signing.h>
#include <aws/crt/http/HttpRequestResponse.h>

namespace Aws
{
    static Crt::ByteCursor s_dateHeader = aws_byte_cursor_from_c_str("x-amz-date");
    static Crt::ByteCursor s_securityTokenHeader = aws_byte_cursor_from_c_str("x-amz-security-token");

    namespace Iot
    {
        WebsocketConfig::WebsocketConfig(
            const Crt::String &signingRegion,
            Crt::Io::ClientBootstrap *bootstrap,
            Crt::Allocator *allocator) noexcept
            : SigningRegion(signingRegion), ServiceName("iotdevicegateway")
        {
            Crt::Auth::CredentialsProviderChainDefaultConfig config;
            config.Bootstrap = bootstrap;

            CredentialsProvider =
                Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(config, allocator);

            Signer = Aws::Crt::MakeShared<Crt::Auth::Sigv4HttpRequestSigner>(allocator, allocator);

            auto credsProviderRef = CredentialsProvider;
            auto signingRegionCopy = SigningRegion;
            auto serviceNameCopy = ServiceName;
            CreateSigningConfigCb = [allocator, credsProviderRef, signingRegionCopy, serviceNameCopy]() {
                auto signerConfig = Aws::Crt::MakeShared<Crt::Auth::AwsSigningConfig>(allocator);
                signerConfig->SetRegion(signingRegionCopy);
                signerConfig->SetService(serviceNameCopy);
                signerConfig->SetSigningAlgorithm(Crt::Auth::SigningAlgorithm::SigV4);
                signerConfig->SetSignatureType(Crt::Auth::SignatureType::HttpRequestViaQueryParams);
                signerConfig->SetSignedBodyValue(Crt::Auth::SignedBodyValueType::Empty);
                signerConfig->SetOmitSessionToken(true);
                signerConfig->SetCredentialsProvider(credsProviderRef);

                return signerConfig;
            };
        }

        WebsocketConfig::WebsocketConfig(
            const Crt::String &signingRegion,
            const std::shared_ptr<Crt::Auth::ICredentialsProvider> &credentialsProvider,
            Crt::Allocator *allocator) noexcept
            : CredentialsProvider(credentialsProvider),
              Signer(Aws::Crt::MakeShared<Crt::Auth::Sigv4HttpRequestSigner>(allocator, allocator)),
              SigningRegion(signingRegion), ServiceName("iotdevicegateway")
        {
            auto credsProviderRef = CredentialsProvider;
            auto signingRegionCopy = SigningRegion;
            auto serviceNameCopy = ServiceName;
            CreateSigningConfigCb = [allocator, credsProviderRef, signingRegionCopy, serviceNameCopy]() {
                auto signerConfig = Aws::Crt::MakeShared<Crt::Auth::AwsSigningConfig>(allocator);
                signerConfig->SetRegion(signingRegionCopy);
                signerConfig->SetService(serviceNameCopy);
                signerConfig->SetSigningAlgorithm(Crt::Auth::SigningAlgorithm::SigV4);
                signerConfig->SetSignatureType(Crt::Auth::SignatureType::HttpRequestViaQueryParams);
                signerConfig->SetSignedBodyValue(Crt::Auth::SignedBodyValueType::Empty);
                signerConfig->SetOmitSessionToken(true);
                signerConfig->SetCredentialsProvider(credsProviderRef);

                return signerConfig;
            };
        }

        WebsocketConfig::WebsocketConfig(
            const std::shared_ptr<Crt::Auth::ICredentialsProvider> &credentialsProvider,
            const std::shared_ptr<Crt::Auth::IHttpRequestSigner> &signer,
            Iot::CreateSigningConfig createConfig) noexcept
            : CredentialsProvider(credentialsProvider), Signer(signer), CreateSigningConfigCb(std::move(createConfig)),
              ServiceName("iotdevicegateway")
        {
        }

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
            : m_endpoint(endpoint), m_port(port), m_context(std::move(tlsContext)), m_socketOptions(socketOptions),
              m_lastError(0)
        {
        }

        MqttClientConnectionConfig::MqttClientConnectionConfig(
            const Crt::String &endpoint,
            uint16_t port,
            const Crt::Io::SocketOptions &socketOptions,
            Crt::Io::TlsContext &&tlsContext,
            Crt::Mqtt::OnWebSocketHandshakeIntercept &&interceptor,
            const Crt::Optional<Crt::Http::HttpClientConnectionProxyOptions> &proxyOptions)
            : m_endpoint(endpoint), m_port(port), m_context(std::move(tlsContext)), m_socketOptions(socketOptions),
              m_webSocketInterceptor(std::move(interceptor)), m_proxyOptions(proxyOptions), m_lastError(0)
        {
        }

        MqttClientConnectionConfigBuilder::MqttClientConnectionConfigBuilder() : m_isGood(false) {}

        MqttClientConnectionConfigBuilder::MqttClientConnectionConfigBuilder(
            const char *certPath,
            const char *pkeyPath,
            Crt::Allocator *allocator) noexcept
            : m_allocator(allocator), m_portOverride(0), m_isGood(true)
        {
            m_socketOptions.SetConnectTimeoutMs(3000);
            m_contextOptions = Crt::Io::TlsContextOptions::InitClientWithMtls(certPath, pkeyPath, allocator);
            if (!m_contextOptions)
            {
                m_isGood = false;
            }
        }

        MqttClientConnectionConfigBuilder::MqttClientConnectionConfigBuilder(
            const Crt::ByteCursor &cert,
            const Crt::ByteCursor &pkey,
            Crt::Allocator *allocator) noexcept
            : m_allocator(allocator), m_portOverride(0), m_isGood(true)
        {
            m_socketOptions.SetConnectTimeoutMs(3000);
            m_contextOptions = Crt::Io::TlsContextOptions::InitClientWithMtls(cert, pkey, allocator);
            if (!m_contextOptions)
            {
                m_isGood = false;
            }
        }

        MqttClientConnectionConfigBuilder::MqttClientConnectionConfigBuilder(
            const WebsocketConfig &config,
            Crt::Allocator *allocator) noexcept
            : m_allocator(allocator), m_portOverride(0), m_isGood(true)
        {
            m_socketOptions.SetConnectTimeoutMs(3000);
            m_contextOptions = Crt::Io::TlsContextOptions::InitDefaultClient(allocator);
            if (!m_contextOptions)
            {
                m_isGood = false;
            }

            m_websocketConfig = config;
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
                    m_isGood = false;
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
                    m_isGood = false;
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

        MqttClientConnectionConfigBuilder &MqttClientConnectionConfigBuilder::WithMinimumTlsVersion(aws_tls_versions minimumTlsVersion) noexcept
        {
            m_contextOptions.SetMinimumTlsVersion(minimumTlsVersion);
            return *this;
        }

        MqttClientConnectionConfig MqttClientConnectionConfigBuilder::Build() noexcept
        {
            if (!m_isGood)
            {
                return MqttClientConnectionConfig::CreateInvalid(aws_last_error());
            }

            uint16_t port = m_portOverride;

            if (!m_portOverride)
            {
                if (m_websocketConfig || Crt::Io::TlsContextOptions::IsAlpnSupported())
                {
                    port = 443;
                }
                else
                {
                    port = 8883;
                }
            }

            if (port == 443 && !m_websocketConfig && Crt::Io::TlsContextOptions::IsAlpnSupported())
            {
                if (!m_contextOptions.SetAlpnList("x-amzn-mqtt-ca"))
                {
                    return MqttClientConnectionConfig::CreateInvalid(Aws::Crt::LastErrorOrUnknown());
                }
            }

            if (!m_websocketConfig)
            {
                return MqttClientConnectionConfig(
                    m_endpoint,
                    port,
                    m_socketOptions,
                    Crt::Io::TlsContext(m_contextOptions, Crt::Io::TlsMode::CLIENT, m_allocator));
            }

            auto websocketConfig = m_websocketConfig.value();
            auto signerTransform = [websocketConfig](
                                       std::shared_ptr<Crt::Http::HttpRequest> req,
                                       const Crt::Mqtt::OnWebSocketHandshakeInterceptComplete &onComplete) {
                // it is only a very happy coincidence that these function signatures match. This is the callback
                // for signing to be complete. It invokes the callback for websocket handshake to be complete.
                auto signingComplete =
                    [onComplete](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &req1, int errorCode) {
                        onComplete(req1, errorCode);
                    };

                auto signerConfig = websocketConfig.CreateSigningConfigCb();

                websocketConfig.Signer->SignRequest(req, *signerConfig, signingComplete);
            };

            return MqttClientConnectionConfig(
                m_endpoint,
                port,
                m_socketOptions,
                Crt::Io::TlsContext(m_contextOptions, Crt::Io::TlsMode::CLIENT, m_allocator),
                signerTransform,
                m_websocketConfig->ProxyOptions);
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

                if (config.m_proxyOptions)
                {
                    newConnection->SetWebsocketProxyOptions(config.m_proxyOptions.value());
                }
            }

            return newConnection;
        }
    } // namespace Iot
} // namespace Aws
