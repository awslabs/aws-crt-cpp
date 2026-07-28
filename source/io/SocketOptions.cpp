/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/io/SocketOptions.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {

            static const uint32_t DEFAULT_SOCKET_TIME_MSEC = 3000;

            SocketOptions::SocketOptions()
            {
                AWS_ZERO_STRUCT(options);
                options.type = AWS_SOCKET_STREAM;
                options.domain = AWS_SOCKET_IPV4;
                options.connect_timeout_ms = DEFAULT_SOCKET_TIME_MSEC;
                options.keep_alive_max_failed_probes = 0;
                options.keep_alive_timeout_sec = 0;
                options.keep_alive_interval_sec = 0;
                options.keepalive = false;
                options.tcp_nodelay = AWS_SOCKET_TCP_NODELAY_ON;
            }
        } // namespace Io
    } // namespace Crt
} // namespace Aws
