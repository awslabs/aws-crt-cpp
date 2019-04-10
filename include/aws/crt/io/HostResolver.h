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
#include <aws/crt/Types.h>

#include <aws/io/host_resolver.h>

#include <functional>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            class EventLoopGroup;
            class HostResolver;

            using HostAddress = aws_host_address;

            /**
             * Invoked upon resolution of an address. You do not own the memory pointed to in addresses, if you persist
             * the data, copy it first. If errorCode is AWS_ERROR_SUCCESS, the operation succeeded. Otherwise, the
             * operation failed.
             */
            using OnHostResolved =
                std::function<void(HostResolver &resolver, const Vector<HostAddress> &addresses, int errorCode)>;

            class HostResolver
            {
              public:
                virtual ~HostResolver();
                virtual bool ResolveHost(const String &host, const OnHostResolved &onResolved) noexcept = 0;
                virtual aws_host_resolver *GetUnderlyingHandle() noexcept = 0;
                virtual aws_host_resolution_config *GetConfig() noexcept = 0;
            };

            class DefaultHostResolver final : public HostResolver
            {
              public:
                /**
                 * Resolves DNS addresses. maxHosts is the number of unique hosts to maintain in the cache. maxTTL is
                 * how long to keep an address in the cache before evicting it.
                 */
                DefaultHostResolver(
                    EventLoopGroup &elGroup,
                    size_t maxHosts,
                    size_t maxTTL,
                    Allocator *allocator = DefaultAllocator()) noexcept;
                ~DefaultHostResolver();
                DefaultHostResolver(const DefaultHostResolver &) = delete;
                DefaultHostResolver &operator=(const DefaultHostResolver &) = delete;
                DefaultHostResolver(DefaultHostResolver &&) = delete;
                DefaultHostResolver &operator=(DefaultHostResolver &&) = delete;

                operator bool() const noexcept { return m_initialized; }

                /**
                 * Kicks off an asynchronous resolution of host. onResolved will be invoked upon completion of the
                 * resolution. If this returns false, the resolution was not attempted. On true, onResolved will be
                 * called with the result.
                 */
                bool ResolveHost(const String &host, const OnHostResolved &onResolved) noexcept override;
                aws_host_resolver *GetUnderlyingHandle() noexcept override { return &m_resolver; }
                aws_host_resolution_config *GetConfig() noexcept override { return &m_config; }

              private:
                aws_host_resolver m_resolver;
                aws_host_resolution_config m_config;
                Allocator *m_allocator;
                bool m_initialized;

                static void s_onHostResolved(
                    struct aws_host_resolver *resolver,
                    const struct aws_string *host_name,
                    int err_code,
                    const struct aws_array_list *host_addresses,
                    void *user_data);
            };
        } // namespace Io
    }     // namespace Crt
} // namespace Aws
