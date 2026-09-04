/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/io/Socks5.h>

#include <aws/io/socks5.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            std::shared_ptr<Socks5ProxyNegotiationStrategy> Socks5ProxyNegotiationStrategy::newStrategyNoAuth(
                Allocator *allocator)
            {
                struct aws_socks5_proxy_negotiation_strategy *strategy =
                    aws_socks5_proxy_negotiation_strategy_new_no_auth(allocator);

                Socks5ProxyNegotiationStrategy *t = reinterpret_cast<Socks5ProxyNegotiationStrategy *>(
                    aws_mem_acquire(allocator, sizeof(Socks5ProxyNegotiationStrategy)));
                new (t) Socks5ProxyNegotiationStrategy(strategy);

                return std::shared_ptr<Socks5ProxyNegotiationStrategy>(
                    t,
                    [allocator](Socks5ProxyNegotiationStrategy *strat)
                    {
                        strat->~Socks5ProxyNegotiationStrategy();
                        aws_mem_release(allocator, strat);
                    });
            }

            std::shared_ptr<Socks5ProxyNegotiationStrategy> Socks5ProxyNegotiationStrategy::newStrategyBasicAuth(
                const Socks5BasicAuthOptions &options,
                Allocator *allocator)
            {
                struct aws_socks5_proxy_negotiation_basic_auth_options basic_auth_options = {
                    .username = options.getUsername().cursor(),
                    .password = options.getPassword().cursor(),
                };
                struct aws_socks5_proxy_negotiation_strategy *strategy =
                    aws_socks5_proxy_negotiation_strategy_new_basic_auth(allocator, &basic_auth_options);

                Socks5ProxyNegotiationStrategy *t = reinterpret_cast<Socks5ProxyNegotiationStrategy *>(
                    aws_mem_acquire(allocator, sizeof(Socks5ProxyNegotiationStrategy)));
                new (t) Socks5ProxyNegotiationStrategy(strategy);

                return std::shared_ptr<Socks5ProxyNegotiationStrategy>(
                    t, [allocator](Socks5ProxyNegotiationStrategy *strat) { Aws::Crt::Delete(strat, allocator); });
            }

            Socks5ProxyNegotiationStrategy::Socks5ProxyNegotiationStrategy(
                struct aws_socks5_proxy_negotiation_strategy *strategy) noexcept
                : m_strategy(strategy)
            {
            }

            Socks5ProxyNegotiationStrategy::~Socks5ProxyNegotiationStrategy()
            {
                aws_socks5_proxy_negotiation_strategy_release(m_strategy);
            }

            Socks5ProxyOptions::Socks5ProxyOptions(
                const Aws::Crt::String &proxyHost,
                uint16_t proxyPort,
                std::shared_ptr<Socks5ProxyNegotiationStrategy> strategy) noexcept
                : m_proxyHost(proxyHost), m_proxyPort(proxyPort), m_strategy(strategy),
                  m_timeout(std::chrono::milliseconds(0))
            {
            }

            Socks5ProxyOptions &Socks5ProxyOptions::withTimeout(std::chrono::milliseconds timeout) noexcept
            {
                m_timeout = timeout;

                return *this;
            }
        } // namespace Io
    } // namespace Crt
} // namespace Aws
