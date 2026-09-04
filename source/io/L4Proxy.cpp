/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/io/L4Proxy.h>

#include "aws/io/l4_proxy.h"
#include "aws/io/socks5.h"
#include <aws/crt/io/Socks5.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            L4ProxyConfig::L4ProxyConfig(struct aws_l4_proxy_config *l4_proxy_config) : m_l4ProxyConfig(l4_proxy_config)
            {
            }

            L4ProxyConfig::~L4ProxyConfig()
            {
                aws_l4_proxy_config_release(m_l4ProxyConfig);
            }

            std::shared_ptr<L4ProxyConfig> L4ProxyConfig::newSocks5ProxyConfig(
                const Socks5ProxyOptions &options,
                Allocator *allocator)
            {
                struct aws_socks5_proxy_options proxy_options = {
                    .proxy_host = aws_byte_cursor_from_c_str(options.proxyHost().c_str()),
                    .proxy_port = options.proxyPort(),
                    .negotiation_strategy = options.strategy()->get(),
                    .negotiation_timeout_ms =
                        static_cast<uint32_t>(aws_min_u64(UINT32_MAX, options.timeout().count()))};

                struct aws_l4_proxy_config *l4_proxy_config = aws_l4_proxy_config_new_socks5(allocator, &proxy_options);

                L4ProxyConfig *t = reinterpret_cast<L4ProxyConfig *>(aws_mem_acquire(allocator, sizeof(L4ProxyConfig)));
                new (t) L4ProxyConfig(l4_proxy_config);

                return std::shared_ptr<L4ProxyConfig>(
                    t, [allocator](L4ProxyConfig *config) { Aws::Crt::Delete(config, allocator); });
            }
        } // namespace Io
    } // namespace Crt
} // namespace Aws