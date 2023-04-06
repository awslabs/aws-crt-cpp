/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/io/HostResolver.h>

#include <aws/crt/io/EventLoopGroup.h>

#include <aws/common/string.h>
#include <aws/crt/Api.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            /**
             * @private
             */
            struct HostResolveArgs
            {
                Allocator *allocator{};
                HostResolver *resolver{};
                OnHostResolved onResolved;
                aws_string *host{};
            };

            HostResolver::~HostResolver() {}

            static void s_onHostResolved(
                struct aws_host_resolver *,
                const struct aws_string *hostName,
                int errCode,
                const struct aws_array_list *hostAddresses,
                void *userData)
            {
                auto *args = static_cast<HostResolveArgs *>(userData);

                size_t len = aws_array_list_length(hostAddresses);
                Vector<HostAddress> addresses;

                for (size_t i = 0; i < len; ++i)
                {
                    HostAddress *address_ptr = nullptr;
                    aws_array_list_get_at_ptr(hostAddresses, reinterpret_cast<void **>(&address_ptr), i);
                    addresses.push_back(*address_ptr);
                }

                String host(aws_string_c_str(hostName), hostName->len);
                args->onResolved(*args->resolver, addresses, errCode);
                aws_string_destroy(args->host);
                Delete(args, args->allocator);
            }

            static bool s_ResolveHostCommon(
                aws_host_resolver *resolver,
                HostResolver *resolverBase,
                const String &host,
                const OnHostResolved &onResolved) noexcept
            {
                auto *allocator = resolver->allocator;
                auto *args = New<HostResolveArgs>(allocator);

                args->host =
                    aws_string_new_from_array(allocator, reinterpret_cast<const uint8_t *>(host.data()), host.length());
                args->onResolved = onResolved;
                args->resolver = resolverBase;
                args->allocator = allocator;

                if (aws_host_resolver_resolve_host(
                        resolver, args->host, s_onHostResolved, resolverBase->GetConfig(), args))
                {
                    Delete(args, allocator);
                    return false;
                }

                return true;
            }

            aws_host_resolver_vtable CustomHostResolverBase::s_vTable = {
                CustomHostResolverBase::s_destroy,
                CustomHostResolverBase::s_resolveHost,
                CustomHostResolverBase::s_recordConnectionFailure,
                CustomHostResolverBase::s_purgeCache,
                nullptr,
                nullptr,
                CustomHostResolverBase::s_getHostAddressCount,
                nullptr,
                nullptr,
            };

            CustomHostResolverBase::CustomHostResolverBase(Allocator *allocator) noexcept
                : m_resolver(nullptr), m_allocator(allocator), m_initialized(false)
            {
                AWS_ZERO_STRUCT(m_config);
                m_resolver = Crt::New<aws_host_resolver>(allocator);
                m_resolver->allocator = allocator;
                m_resolver->vtable = &s_vTable;
                m_resolver->impl = (void *)this;
                aws_ref_count_init(&m_resolver->ref_count, m_resolver, CustomHostResolverBase::s_atomicRelease);
                m_initialized = true;
            }

            CustomHostResolverBase::~CustomHostResolverBase()
            {
                aws_host_resolver_release(m_resolver);
            }

            void CustomHostResolverBase::s_atomicRelease(void *resolver)
            {
                auto *hostResolver = reinterpret_cast<aws_host_resolver *>(resolver);
                CustomHostResolverBase::s_destroy(hostResolver);
            }

            void CustomHostResolverBase::s_destroy(struct aws_host_resolver *resolver)
            {
                auto *base = reinterpret_cast<CustomHostResolverBase *>(resolver->impl);
                base->m_initialized = false;
                Delete(resolver, resolver->allocator);
            }

            bool CustomHostResolverBase::ResolveHost(const String &host, const OnHostResolved &onResolved) noexcept
            {
                return s_ResolveHostCommon(m_resolver, this, host, onResolved);
            }

            int CustomHostResolverBase::s_resolveHost(
                struct aws_host_resolver *resolver,
                const struct aws_string *hostName,
                aws_on_host_resolved_result_fn *res,
                const struct aws_host_resolution_config *,
                void *user_data)
            {
                auto *base = reinterpret_cast<CustomHostResolverBase *>(resolver->impl);
                String host(aws_string_c_str(hostName), hostName->len);

                auto *allocator = base->m_allocator;
                struct aws_string *hostNameCpy =
                    aws_string_new_from_array(allocator, reinterpret_cast<const uint8_t *>(host.data()), host.length());

                auto onHostResolved = [allocator, resolver, hostNameCpy, res, user_data](
                                          HostResolver &, const Vector<HostAddress> &addresses, int errorCode)
                {
                    aws_array_list hostAddressList;
                    AWS_FATAL_ASSERT(
                        aws_array_list_init_dynamic(
                            &hostAddressList, allocator, addresses.size(), sizeof(aws_host_address)) == AWS_OP_SUCCESS);

                    for (const auto &addr : addresses)
                    {
                        aws_host_address cpy_to;
                        AWS_ZERO_STRUCT(cpy_to);
                        aws_host_address_copy(&addr, &cpy_to);
                        aws_array_list_push_back(&hostAddressList, reinterpret_cast<void *>(&cpy_to));
                    }
                    res(resolver, hostNameCpy, errorCode, &hostAddressList, user_data);

                    for (size_t i = 0; i < aws_array_list_length(&hostAddressList); ++i)
                    {
                        aws_host_address *addr = nullptr;
                        AWS_FATAL_ASSERT(
                            aws_array_list_get_at_ptr(&hostAddressList, reinterpret_cast<void **>(&addr), i) ==
                            AWS_OP_SUCCESS);
                        aws_host_address_clean_up(addr);
                    }

                    aws_array_list_clean_up(&hostAddressList);
                    aws_string_destroy(hostNameCpy);
                };

                return base->OnResolveHost(host, onHostResolved);
            }

            int CustomHostResolverBase::s_recordConnectionFailure(
                struct aws_host_resolver *resolver,
                const struct aws_host_address *address)
            {
                auto *base = reinterpret_cast<CustomHostResolverBase *>(resolver->impl);
                return base->OnRecordConnectionFailure(*address);
            }

            int CustomHostResolverBase::s_purgeCache(struct aws_host_resolver *resolver)
            {
                auto *base = reinterpret_cast<CustomHostResolverBase *>(resolver->impl);
                return base->OnPurgeCache();
            }

            size_t CustomHostResolverBase::s_getHostAddressCount(
                struct aws_host_resolver *resolver,
                const struct aws_string *host_name,
                uint32_t flags)
            {
                auto *base = reinterpret_cast<CustomHostResolverBase *>(resolver->impl);
                String hostName(aws_string_c_str(host_name), host_name->len);
                return base->GetHostAddressCount(hostName, flags);
            }

            DefaultHostResolver::DefaultHostResolver(
                EventLoopGroup &elGroup,
                size_t maxHosts,
                size_t maxTTL,
                Allocator *allocator) noexcept
                : m_resolver(nullptr), m_allocator(allocator), m_initialized(false)
            {
                AWS_ZERO_STRUCT(m_config);

                struct aws_host_resolver_default_options resolver_options;
                AWS_ZERO_STRUCT(resolver_options);
                resolver_options.max_entries = maxHosts;
                resolver_options.el_group = elGroup.GetUnderlyingHandle();

                m_resolver = aws_host_resolver_new_default(allocator, &resolver_options);
                if (m_resolver != nullptr)
                {
                    m_initialized = true;
                }

                m_config.impl = aws_default_dns_resolve;
                m_config.impl_data = nullptr;
                m_config.max_ttl = maxTTL;
            }

            DefaultHostResolver::DefaultHostResolver(size_t maxHosts, size_t maxTTL, Allocator *allocator) noexcept
                : DefaultHostResolver(
                      *Crt::ApiHandle::GetOrCreateStaticDefaultEventLoopGroup(),
                      maxHosts,
                      maxTTL,
                      allocator)
            {
            }

            DefaultHostResolver::~DefaultHostResolver()
            {
                aws_host_resolver_release(m_resolver);
                m_initialized = false;
            }

            bool DefaultHostResolver::ResolveHost(const String &host, const OnHostResolved &onResolved) noexcept
            {
                return s_ResolveHostCommon(m_resolver, this, host, onResolved);
            }
        } // namespace Io
    }     // namespace Crt
} // namespace Aws
