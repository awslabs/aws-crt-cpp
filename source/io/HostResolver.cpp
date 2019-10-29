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
#include <aws/crt/io/HostResolver.h>

#include <aws/crt/io/EventLoopGroup.h>

#include <aws/common/string.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            HostResolver::~HostResolver() {}

            DefaultHostResolver::DefaultHostResolver(
                EventLoopGroup &elGroup,
                size_t maxHosts,
                size_t maxTTL,
                Allocator *allocator) noexcept
                : m_allocator(allocator), m_initialized(true)
            {
                if (aws_host_resolver_init_default(&m_resolver, allocator, maxHosts, elGroup.GetUnderlyingHandle()))
                {
                    m_initialized = false;
                }

                m_config.impl = aws_default_dns_resolve;
                m_config.impl_data = nullptr;
                m_config.max_ttl = maxTTL;
            }

            DefaultHostResolver::~DefaultHostResolver()
            {
                if (m_initialized)
                {
                    aws_host_resolver_clean_up(&m_resolver);
                    m_initialized = false;
                }
            }

            struct DefaultHostResolveArgs
            {
                Allocator *allocator;
                HostResolver *resolver;
                OnHostResolved onResolved;
                aws_string *host;
            };

            void DefaultHostResolver::s_onHostResolved(
                struct aws_host_resolver *,
                const struct aws_string *hostName,
                int errCode,
                const struct aws_array_list *hostAddresses,
                void *userData)
            {
                DefaultHostResolveArgs *args = static_cast<DefaultHostResolveArgs *>(userData);

                size_t len = aws_array_list_length(hostAddresses);
                Vector<HostAddress> addresses;

                for (size_t i = 0; i < len; ++i)
                {
                    HostAddress *address_ptr = NULL;
                    aws_array_list_get_at_ptr(hostAddresses, reinterpret_cast<void **>(&address_ptr), i);
                    addresses.push_back(*address_ptr);
                }

                String host(aws_string_c_str(hostName), hostName->len);
                args->onResolved(*args->resolver, addresses, errCode);
                aws_string_destroy(args->host);
                Delete(args, args->allocator);
            }

            bool DefaultHostResolver::ResolveHost(const String &host, const OnHostResolved &onResolved) noexcept
            {
                DefaultHostResolveArgs *args = New<DefaultHostResolveArgs>(m_allocator);
                if (!args)
                {
                    return false;
                }

                args->host = aws_string_new_from_array(
                    m_allocator, reinterpret_cast<const uint8_t *>(host.data()), host.length());
                args->onResolved = onResolved;
                args->resolver = this;
                args->allocator = m_allocator;

                if (!args->host ||
                    aws_host_resolver_resolve_host(&m_resolver, args->host, s_onHostResolved, &m_config, args))
                {
                    Delete(args, m_allocator);
                    return false;
                }

                return true;
            }
        } // namespace Io
    }     // namespace Crt
} // namespace Aws
