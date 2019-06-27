#pragma once
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
#include <aws/crt/Exports.h>
#include <aws/crt/mqtt/MqttClient.h>

namespace Aws
{
    namespace Iot
    {
        class MqttClient;

        /**
         * Represents a unique configuration for connecting to a single endpoint. You can use a single instance of this
         * class PER endpoint you want to connect to. This object must live through the lifetime of your connection.
         */
        class AWS_CRT_CPP_API MqttClientConnectionConfig final
        {
          public:
            MqttClientConnectionConfig(
                const Crt::String &endpoint,
                uint16_t port,
                const Crt::Io::SocketOptions &socketOptions,
                Crt::Io::TlsContext &&tlsContext);

            explicit operator bool() const noexcept { return m_context ? true : false; }

          private:
            Crt::String m_endpoint;
            uint16_t m_port;
            Crt::Io::TlsContext m_context;
            Crt::Io::SocketOptions m_socketOptions;

            friend class MqttClient;
        };

        /**
         * Represents configuration parameters for building a MqttClientConnectionConfig object. You can use a single
         * instance of this class PER MqttClientConnectionConfig you want to generate. If you want to generate a config
         * for a different endpoint or port etc... you need a new instance of this class.
         */
        class AWS_CRT_CPP_API MqttClientConnectionConfigBuilder final
        {
          public:
            /**
             * Sets the builder up for MTLS using certPath and pkeyPath. These are files on disk and must be in the PEM
             * format.
             */
            MqttClientConnectionConfigBuilder(
                const char *certPath,
                const char *pkeyPath,
                Crt::Allocator *allocator = Crt::DefaultAllocator()) noexcept;
            /**
             * Sets the builder up for MTLS using cert and pkey. These are in-memory buffers and must be in the PEM
             * format.
             */
            MqttClientConnectionConfigBuilder(
                const Crt::ByteCursor &cert,
                const Crt::ByteCursor &pkey,
                Crt::Allocator *allocator = Crt::DefaultAllocator()) noexcept;

            /**
             * Sets endpoint to connect to.
             */
            MqttClientConnectionConfigBuilder &WithEndpoint(const Crt::String &endpoint);

            /**
             * Sets endpoint to connect to.
             */
            MqttClientConnectionConfigBuilder &WithEndpoint(Crt::String &&endpoint);

            /**
             * Overrides the default port. By default, if ALPN is supported, 443 will be used. Otherwise 8883 will be
             * used. If you specify 443 and ALPN is not supported, we will still attempt to connect over 443 without
             * ALPN.
             */
            MqttClientConnectionConfigBuilder &WithPortOverride(uint16_t port) noexcept;

            /**
             * Sets the certificate authority for the endpoint you're connecting to. This is a path to a file on disk
             * and must be in PEM format.
             */
            MqttClientConnectionConfigBuilder &WithCertificateAuthority(const char *caPath) noexcept;

            /**
             * Sets the certificate authority for the endpoint you're connecting to. This is an in-memory buffer and
             * must be in PEM format.
             */
            MqttClientConnectionConfigBuilder &WithCertificateAuthority(const Crt::ByteCursor &cert) noexcept;

            /** TCP option: Enables TCP keep alive. Defaults to off. */
            MqttClientConnectionConfigBuilder &WithTcpKeepAlive() noexcept;

            /** TCP option: Sets the connect timeout. Defaults to 3 seconds. */
            MqttClientConnectionConfigBuilder &WithTcpConnectTimeout(uint32_t connectTimeoutMs) noexcept;

            /** TCP option: Sets time before keep alive probes are sent. Defaults to kernel defaults */
            MqttClientConnectionConfigBuilder &WithTcpKeepAliveTimeout(uint16_t keepAliveTimeoutSecs) noexcept;

            /**
             * TCP option: Sets the frequency of sending keep alive probes in seconds once the keep alive timeout
             * expires. Defaults to kernel defaults.
             */
            MqttClientConnectionConfigBuilder &WithTcpKeepAliveInterval(uint16_t keepAliveIntervalSecs) noexcept;

            /**
             * TCP option: Sets the amount of keep alive probes allowed to fail before the connection is terminated.
             * Defaults to kernel defaults.
             */
            MqttClientConnectionConfigBuilder &WithTcpKeepAliveMaxProbes(uint16_t maxProbes) noexcept;

            /**
             * Builds a client configuration object from the set options.
             */
            MqttClientConnectionConfig Build() noexcept;

          private:
            Crt::Allocator *m_allocator;
            Crt::String m_endpoint;
            uint16_t m_portOverride;
            Crt::Io::SocketOptions m_socketOptions;
            Crt::Io::TlsContextOptions m_contextOptions;
        };

        /**
         * AWS IOT specific Mqtt Client. Sets defaults for using the AWS IOT service. You'll need an instance of
         * MqttClientConnectionConfig to use. Once NewConnection returns, you use it's return value identically
         * to how you would use Aws::Crt::Mqtt::MqttConnection
         */
        class AWS_CRT_CPP_API MqttClient final
        {
          public:
            MqttClient(
                Crt::Io::ClientBootstrap &bootstrap,
                Crt::Allocator *allocator = Crt::DefaultAllocator()) noexcept;

            std::shared_ptr<Crt::Mqtt::MqttConnection> NewConnection(const MqttClientConnectionConfig &config) noexcept;

            int LastError() const noexcept { return m_client.LastError(); }
            explicit operator bool() const noexcept { return m_client ? true : false; }

          private:
            Crt::Mqtt::MqttClient m_client;
        };
    } // namespace Iot
} // namespace Aws