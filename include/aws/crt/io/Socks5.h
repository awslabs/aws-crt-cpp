#pragma once

/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/ByteBufUtils.h>

struct aws_socks5_proxy_negotiation_strategy;

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            class Socks5ProxyOptions;

            /**
             * Holds configuration options relevant to performing basic authentication with a SOCKS5 proxy.
             *
             * See https://www.rfc-editor.org/info/rfc1929/
             */
            class Socks5BasicAuthOptions
            {
              public:
                Socks5BasicAuthOptions() noexcept = default;
                Socks5BasicAuthOptions(const Socks5BasicAuthOptions &rhs) noexcept = default;
                Socks5BasicAuthOptions(Socks5BasicAuthOptions &&rhs) noexcept = default;
                Socks5BasicAuthOptions &operator=(const Socks5BasicAuthOptions &rhs) noexcept = default;
                Socks5BasicAuthOptions &operator=(Socks5BasicAuthOptions &&rhs) noexcept = default;

                ~Socks5BasicAuthOptions() = default;

                /**
                 * Sets the username to use during basic authentication
                 *
                 * @param username username to use
                 * @return the configuration options object
                 */
                Socks5BasicAuthOptions &withUsername(ManagedByteBuffer username) noexcept
                {
                    m_username = std::move(username);
                    return *this;
                }

                /**
                 * Sets the password to use during basic authentication
                 *
                 * @param password password to use
                 * @return the configuration options object
                 */
                Socks5BasicAuthOptions &withPassword(ManagedByteBuffer password) noexcept
                {
                    m_password = std::move(password);
                    return *this;
                }

                /**
                 * Gets the username that will be used in basic authentication
                 *
                 * @return the username that will be used in basic authentication
                 */
                const ManagedByteBuffer &getUsername() const noexcept { return m_username; }

                /**
                 * Gets the password that will be used in basic authentication
                 *
                 * @return the password that will be used in basic authentication
                 */
                const ManagedByteBuffer &getPassword() const noexcept { return m_password; }

              private:
                ManagedByteBuffer m_username;
                ManagedByteBuffer m_password;
            };

            /**
             * Opaque wrapper that represents an authentication strategy to use when negotiating a tunnel through
             * a SOCKS5 proxy.  Currently, only no-authentication and basic authentication are supported.
             */
            class Socks5ProxyNegotiationStrategy
            {
              public:
                Socks5ProxyNegotiationStrategy() noexcept = delete;
                Socks5ProxyNegotiationStrategy(const Socks5ProxyNegotiationStrategy &rhs) noexcept = delete;
                Socks5ProxyNegotiationStrategy(Socks5ProxyNegotiationStrategy &&rhs) noexcept = delete;
                Socks5ProxyNegotiationStrategy &operator=(const Socks5ProxyNegotiationStrategy &rhs) noexcept = delete;
                Socks5ProxyNegotiationStrategy &operator=(Socks5ProxyNegotiationStrategy &&rhs) noexcept = delete;

                ~Socks5ProxyNegotiationStrategy();

                /**
                 * Creates a new negotiation strategy that will not perform any authentication when creating a tunnel
                 * though a SOCKS5 proxy.
                 *
                 * @param allocator memory allocator to use
                 * @return a new SOCKS5 authentication negotiation strategy instance
                 */
                static std::shared_ptr<Socks5ProxyNegotiationStrategy> newStrategyNoAuth(
                    Allocator *allocator = ApiAllocator());

                /**
                 * Creates a new negotiation strategy that will use basic authentication when creating a tunnel
                 * through a SOCKS5 proxy.
                 *
                 * @param options basic authentication options to use
                 * @param allocator memory allocator to use
                 * @return a new SOCKS5 authentication negotiation strategy instance
                 */
                static std::shared_ptr<Socks5ProxyNegotiationStrategy> newStrategyBasicAuth(
                    const Socks5BasicAuthOptions &options,
                    Allocator *allocator = ApiAllocator());

                /**
                 * @internal
                 *
                 * Gets a raw pointer to the C implementation of the negotiation strategy.
                 *
                 * @return the raw pointer to the C implementation of the negotiation strategy
                 */
                struct aws_socks5_proxy_negotiation_strategy *get() const { return m_strategy; }

              private:
                Socks5ProxyNegotiationStrategy(struct aws_socks5_proxy_negotiation_strategy *strategy) noexcept;

                struct aws_socks5_proxy_negotiation_strategy *m_strategy;
            };

            /**
             * Configuration options relevant to routing connections through a SOCKS5 proxy.
             */
            class Socks5ProxyOptions
            {
              public:
                /**
                 * Constructor for a new instance of SOCKS5 proxy configuration options
                 *
                 * @param proxyHost hostname of the SOCKS5 proxy
                 * @param proxyPort listening port of the SOCKS5 proxy
                 * @param strategy authentication strategy to use
                 */
                Socks5ProxyOptions(
                    const Aws::Crt::String &proxyHost,
                    uint16_t proxyPort,
                    std::shared_ptr<Socks5ProxyNegotiationStrategy> strategy) noexcept;
                Socks5ProxyOptions(const Socks5ProxyOptions &rhs) noexcept = default;
                Socks5ProxyOptions(Socks5ProxyOptions &&rhs) noexcept = default;
                Socks5ProxyOptions &operator=(const Socks5ProxyOptions &rhs) noexcept = default;
                Socks5ProxyOptions &operator=(Socks5ProxyOptions &&rhs) noexcept = default;

                ~Socks5ProxyOptions() = default;

                /**
                 * Sets the maximum amount of time to wait before a SOCKS5 negotiation attempt is considered failed
                 *
                 * @param timeout maximum amount of time to wait for a successful negotiation
                 * @return the configuration options instance
                 */
                Socks5ProxyOptions &withTimeout(std::chrono::milliseconds timeout) noexcept;

                /**
                 * Gets the hostname of the SOCKS5 proxy
                 *
                 * @return the hostname of the SOCKS5 proxy
                 */
                const Aws::Crt::String &proxyHost() const noexcept { return m_proxyHost; }

                /**
                 * Gets the listening port of the SOCKS5 proxy
                 *
                 * @return the listening port of the SOCKS5 proxy
                 */
                uint16_t proxyPort() const noexcept { return m_proxyPort; }

                /**
                 * Gets the authentication strategy to use when tunneling through the SOCKS5 proxy
                 *
                 * @return the authentication strategy to use when tunneling through the SOCKS5 proxy
                 */
                std::shared_ptr<Socks5ProxyNegotiationStrategy> strategy() const noexcept { return m_strategy; }

                /**
                 * Gets the maximum amount of time to wait for a successful negotiation
                 *
                 * @return the maximum amount of time to wait for a successful negotiation
                 */
                std::chrono::milliseconds timeout() const noexcept { return m_timeout; }

              private:
                Aws::Crt::String m_proxyHost;
                uint16_t m_proxyPort;

                std::shared_ptr<Socks5ProxyNegotiationStrategy> m_strategy;

                std::chrono::milliseconds m_timeout;
            };

        } // namespace Io
    } // namespace Crt
} // namespace Aws