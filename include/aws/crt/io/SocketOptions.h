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

#include <aws/io/socket.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            class SocketOptions
            {
              public:
                SocketOptions();
                SocketOptions(const SocketOptions &rhs) = default;
                SocketOptions(SocketOptions &&rhs) = default;

                SocketOptions &operator=(const SocketOptions &rhs) = default;
                SocketOptions &operator=(SocketOptions &&rhs) = default;

                void SetSocketType(aws_socket_type type) { options.type = type; }
                aws_socket_type GetSocketType() const { return options.type; }

                void SetSocketDomain(aws_socket_domain domain) { options.domain = domain; }
                aws_socket_domain GetSocketDomain() const { return options.domain; }

                void SetConnectTimeoutMs(uint32_t timeout) { options.connect_timeout_ms = timeout; }
                uint32_t GetConnectTimeoutMs() const { return options.connect_timeout_ms; }

                void SetKeepAliveIntervalSec(uint16_t keepAliveInterval)
                {
                    options.keep_alive_interval_sec = keepAliveInterval;
                }
                uint16_t GetKeepAliveIntervalSec() const { return options.keep_alive_interval_sec; }

                void SetKeepAliveTimeoutSec(uint16_t keepAliveTimeout)
                {
                    options.keep_alive_timeout_sec = keepAliveTimeout;
                }
                uint16_t GetKeepAliveTimeoutSec() const { return options.keep_alive_timeout_sec; }

                void SetKeepAliveMaxFailedProbes(uint16_t maxProbes)
                {
                    options.keep_alive_max_failed_probes = maxProbes;
                }
                uint16_t GetKeepAliveMaxFailedProbes() const { return options.keep_alive_max_failed_probes; }

                void SetKeepAlive(bool keepAlive) { options.keepalive = keepAlive; }
                bool GetKeepAlive() const { return options.keepalive; }

                aws_socket_options &GetImpl() { return options; }
                const aws_socket_options &GetImpl() const { return options; }

              private:
                aws_socket_options options;
            };
        } // namespace Io
    }     // namespace Crt
} // namespace Aws