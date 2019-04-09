/*
 * Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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
#include <aws/crt/io/Bootstrap.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            ClientBootstrap::ClientBootstrap(EventLoopGroup &elGroup, Allocator *allocator) noexcept
                : m_lastError(AWS_ERROR_SUCCESS)
            {
                aws_host_resolver_init_default(&m_resolver, allocator, 64);
                m_resolve_config.impl = aws_default_dns_resolve;
                m_resolve_config.impl_data = nullptr;
                m_resolve_config.max_ttl = 30;
                m_bootstrap =
                    aws_client_bootstrap_new(allocator, elGroup.GetUnderlyingHandle(), &m_resolver, &m_resolve_config);
            }

            ClientBootstrap::~ClientBootstrap()
            {
                if (m_bootstrap)
                {
                    aws_host_resolver_clean_up(&m_resolver);
                    aws_client_bootstrap_release(m_bootstrap);
                    m_bootstrap = nullptr;
                    m_lastError = AWS_ERROR_UNKNOWN;
                    AWS_ZERO_STRUCT(m_bootstrap);
                }
            }

            ClientBootstrap::operator bool() const noexcept { return m_lastError == AWS_ERROR_SUCCESS; }

            int ClientBootstrap::LastError() const noexcept { return m_lastError; }

            aws_client_bootstrap *ClientBootstrap::GetUnderlyingHandle() noexcept
            {
                if (*this)
                {
                    return m_bootstrap;
                }

                return nullptr;
            }
        } // namespace Io
    }     // namespace Crt
} // namespace Aws
