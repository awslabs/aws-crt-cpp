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

            class Socks5BasicAuthOptions
            {
              public:
                Socks5BasicAuthOptions() noexcept = default;
                Socks5BasicAuthOptions(const Socks5BasicAuthOptions &rhs) noexcept = default;
                Socks5BasicAuthOptions(Socks5BasicAuthOptions &&rhs) noexcept = default;
                Socks5BasicAuthOptions &operator=(const Socks5BasicAuthOptions &rhs) noexcept = default;
                Socks5BasicAuthOptions &operator=(Socks5BasicAuthOptions &&rhs) noexcept = default;

                ~Socks5BasicAuthOptions() = default;

                Socks5BasicAuthOptions &withUsername(ManagedByteBuffer username) noexcept
                {
                    m_username = std::move(username);
                    return *this;
                }

                Socks5BasicAuthOptions &withPassword(ManagedByteBuffer password) noexcept
                {
                    m_password = std::move(password);
                    return *this;
                }

                const ManagedByteBuffer &getUsername() const noexcept { return m_username; }

                const ManagedByteBuffer &getPassword() const noexcept { return m_password; }

              private:
                ManagedByteBuffer m_username;
                ManagedByteBuffer m_password;
            };

            class Socks5ProxyNegotiationStrategy
            {
              public:
                Socks5ProxyNegotiationStrategy() noexcept = delete;
                Socks5ProxyNegotiationStrategy(const Socks5ProxyNegotiationStrategy &rhs) noexcept = delete;
                Socks5ProxyNegotiationStrategy(Socks5ProxyNegotiationStrategy &&rhs) noexcept = delete;
                Socks5ProxyNegotiationStrategy &operator=(const Socks5ProxyNegotiationStrategy &rhs) noexcept = delete;
                Socks5ProxyNegotiationStrategy &operator=(Socks5ProxyNegotiationStrategy &&rhs) noexcept = delete;

                ~Socks5ProxyNegotiationStrategy();

                static std::shared_ptr<Socks5ProxyNegotiationStrategy> newStrategyNoAuth(
                    Allocator *allocator = ApiAllocator());
                static std::shared_ptr<Socks5ProxyNegotiationStrategy> newStrategyBasicAuth(
                    const Socks5BasicAuthOptions &options,
                    Allocator *allocator = ApiAllocator());

                struct aws_socks5_proxy_negotiation_strategy *get() const { return m_strategy; }

              private:
                Socks5ProxyNegotiationStrategy(struct aws_socks5_proxy_negotiation_strategy *strategy) noexcept;

                struct aws_socks5_proxy_negotiation_strategy *m_strategy;
            };

            class Socks5ProxyOptions
            {
              public:
                Socks5ProxyOptions(
                    const Aws::Crt::String &proxyHost,
                    uint16_t proxyPort,
                    std::shared_ptr<Socks5ProxyNegotiationStrategy> strategy) noexcept;
                Socks5ProxyOptions(const Socks5ProxyOptions &rhs) noexcept = default;
                Socks5ProxyOptions(Socks5ProxyOptions &&rhs) noexcept = default;
                Socks5ProxyOptions &operator=(const Socks5ProxyOptions &rhs) noexcept = default;
                Socks5ProxyOptions &operator=(Socks5ProxyOptions &&rhs) noexcept = default;

                ~Socks5ProxyOptions() = default;

                Socks5ProxyOptions &withTimeout(std::chrono::milliseconds timeout) noexcept;

                const Aws::Crt::String &proxyHost() const noexcept { return m_proxyHost; }
                uint16_t proxyPort() const noexcept { return m_proxyPort; }
                std::shared_ptr<Socks5ProxyNegotiationStrategy> strategy() const noexcept { return m_strategy; }
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