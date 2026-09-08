#pragma once

/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Types.h>

struct aws_l4_proxy_config;

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            class Socks5ProxyOptions;

            /**
             * Wrapper around a C implementation of an object capable of routing connection attempts properly
             * through an L4 proxy.  Only SOCKS5 is supported.
             */
            class L4ProxyConfig
            {
              public:
                L4ProxyConfig() noexcept = delete;
                L4ProxyConfig(const L4ProxyConfig &rhs) noexcept = delete;
                L4ProxyConfig(L4ProxyConfig &&rhs) noexcept = delete;
                L4ProxyConfig &operator=(const L4ProxyConfig &rhs) noexcept = delete;
                L4ProxyConfig &operator=(L4ProxyConfig &&rhs) noexcept = delete;

                ~L4ProxyConfig();

                /**
                 * Creates a new proxy config instance for tunneling connections through a SOCKS5 proxy
                 *
                 * @param options configuration options related to SOCKS5 proxy negotiation
                 * @param allocator memory allocator to use
                 * @return a new proxy configuration instance
                 */
                static std::shared_ptr<L4ProxyConfig> newSocks5ProxyConfig(
                    const Socks5ProxyOptions &options,
                    Allocator *allocator = ApiAllocator());

                /**
                 * Gets the C implementation of the proxy negotiation implementation
                 * @internal
                 *
                 * @return the C implementation of the proxy negotiation implementation
                 */
                struct aws_l4_proxy_config *get() const noexcept { return m_l4ProxyConfig; }

              private:
                L4ProxyConfig(struct aws_l4_proxy_config *l4_proxy_config);

                struct aws_l4_proxy_config *m_l4ProxyConfig;
            };
        } // namespace Io
    } // namespace Crt
} // namespace Aws
