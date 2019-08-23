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
                options.type = AWS_SOCKET_STREAM;
                options.domain = AWS_SOCKET_IPV4;
                options.connect_timeout_ms = DEFAULT_SOCKET_TIME_MSEC;
                options.keep_alive_max_failed_probes = 0;
                options.keep_alive_timeout_sec = 0;
                options.keep_alive_interval_sec = 0;
                options.keepalive = false;
            }
        } // namespace Io
    }     // namespace Crt
} // namespace Aws