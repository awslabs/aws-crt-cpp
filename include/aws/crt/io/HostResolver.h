#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
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

            /**
             * Simple interface for DNS name lookup implementations
             */
            class AWS_CRT_CPP_API HostResolver
            {
              public:
                virtual ~HostResolver();
                virtual bool ResolveHost(const String &host, const OnHostResolved &onResolved) noexcept = 0;

                /// @private
                virtual aws_host_resolver *GetUnderlyingHandle() noexcept = 0;
                /// @private
                virtual aws_host_resolution_config *GetConfig() noexcept = 0;
            };

            /**
             * Interface for providing your own DNS resolver. This allows you to entirely swap out the DNS
             * implementation for the entire system. To do so, inherit from this class and implement each of the
             * protected functions. Keep in mind that none of these functions are allowed to block.
             */
            class AWS_CRT_CPP_API CustomHostResolverBase : public HostResolver
            {
              public:
                explicit CustomHostResolverBase(Allocator *allocator = ApiAllocator()) noexcept;
                virtual ~CustomHostResolverBase() override;

                /**
                 * Kicks off an asynchronous resolution of host. onResolved will be invoked upon completion of the
                 * resolution.
                 * @return False, the resolution was not attempted. True, onResolved will be
                 * called with the result.
                 */
                bool ResolveHost(const String &host, const OnHostResolved &onResolved) noexcept override;

                /// @private
                aws_host_resolver *GetUnderlyingHandle() noexcept override { return m_resolver; }

                /// @private
                aws_host_resolution_config *GetConfig() noexcept override { return &m_config; }

                explicit operator bool() const noexcept { return m_initialized; }

                /**
                 * @return the value of the last aws error encountered by operations on this instance.
                 */
                int LastError() const noexcept { return aws_last_error(); }

              protected:
                Allocator *m_allocator;

                /**
                 * Invoked upon the system needing to resolve host. This function MUST NOT block or this will stall
                 * the event-loops. Invoke onResolved with the results upon completion.
                 *
                 * Returns int similar to other aws-c-* library functions. Return AWS_OP_SUCCESS upon success or
                 * aws_raise_error()/AWS_OP_ERR with the appropriate error codes from aws-c-common or aws-c-io on
                 * failure.
                 *
                 * Success for this function is defined as basic validation checks and that the asynchronous request was
                 * queued.
                 */
                virtual int OnResolveHost(const String &host, const OnHostResolved &onResolved) noexcept = 0;

                /**
                 * Invoked upon the system noticing an IP address that is having connection failures. This gives you an
                 * opportunity to migrate traffic to a different set of addresses for future resolves.
                 * This function MUST NOT block or this will stall the event-loops.                 *
                 *
                 * Returns int similar to other aws-c-* library functions. Return AWS_OP_SUCCESS upon success or
                 * aws_raise_error()/AWS_OP_ERR with the appropriate error codes from aws-c-common or aws-c-io on
                 * failure.
                 */
                virtual int OnRecordConnectionFailure(const HostAddress &address) noexcept = 0;

                /**
                 * Whatever has been cached. Delete it and start over.
                 *
                 * Returns int similar to other aws-c-* library functions. Return AWS_OP_SUCCESS upon success or
                 * aws_raise_error()/AWS_OP_ERR with the appropriate error codes from aws-c-common or aws-c-io on
                 * failure.
                 */
                virtual int OnPurgeCache() noexcept = 0;

                /**
                 * Returns the number of addresses for hostName. Flags is a big field of enum aws_address_record_type.
                 * Check the bitfield for the types of addresses being queried for and only include those types in your
                 * count.
                 */
                virtual size_t GetHostAddressCount(const String &hostName, uint32_t flags) noexcept = 0;

              private:
                aws_host_resolver *m_resolver;
                aws_host_resolution_config m_config;
                bool m_initialized;

                static aws_host_resolver_vtable s_vTable;

                static void s_destroy(aws_host_resolver *resolver);
                static void s_atomicRelease(void *resolver);

                static int s_resolveHost(
                    aws_host_resolver *resolver,
                    const aws_string *host_name,
                    aws_on_host_resolved_result_fn *res,
                    const aws_host_resolution_config *config,
                    void *user_data);

                static int s_recordConnectionFailure(aws_host_resolver *resolver, const aws_host_address *address);

                static int s_purgeCache(aws_host_resolver *resolver);

                static size_t s_getHostAddressCount(
                    aws_host_resolver *resolver,
                    const aws_string *host_name,
                    uint32_t flags);
            };

            /**
             * A wrapper around the CRT default host resolution system that uses getaddrinfo() farmed off
             * to separate threads in order to resolve names.
             */
            class AWS_CRT_CPP_API DefaultHostResolver final : public HostResolver
            {
              public:
                /**
                 * Resolves DNS addresses.
                 *
                 * @param elGroup: EventLoopGroup to use.
                 * @param maxHosts: the number of unique hosts to maintain in the cache.
                 * @param maxTTL: how long to keep an address in the cache before evicting it.
                 * @param allocator memory allocator to use.
                 */
                DefaultHostResolver(
                    EventLoopGroup &elGroup,
                    size_t maxHosts,
                    size_t maxTTL,
                    Allocator *allocator = ApiAllocator()) noexcept;

                /**
                 * Resolves DNS addresses using the default EventLoopGroup.
                 *
                 * For more information on the default EventLoopGroup see
                 * Aws::Crt::ApiHandle::GetOrCreateStaticDefaultEventLoopGroup
                 *
                 * @param maxHosts: the number of unique hosts to maintain in the cache.
                 * @param maxTTL: how long to keep an address in the cache before evicting it.
                 * @param allocator memory allocator to use.
                 */
                DefaultHostResolver(size_t maxHosts, size_t maxTTL, Allocator *allocator = ApiAllocator()) noexcept;

                ~DefaultHostResolver();
                DefaultHostResolver(const DefaultHostResolver &) = delete;
                DefaultHostResolver &operator=(const DefaultHostResolver &) = delete;
                DefaultHostResolver(DefaultHostResolver &&) = delete;
                DefaultHostResolver &operator=(DefaultHostResolver &&) = delete;

                /**
                 * @return true if the instance is in a valid state, false otherwise.
                 */
                operator bool() const noexcept { return m_initialized; }

                /**
                 * @return the value of the last aws error encountered by operations on this instance.
                 */
                int LastError() const noexcept { return aws_last_error(); }

                /**
                 * Kicks off an asynchronous resolution of host. onResolved will be invoked upon completion of the
                 * resolution.
                 * @return False, the resolution was not attempted. True, onResolved will be
                 * called with the result.
                 */
                bool ResolveHost(const String &host, const OnHostResolved &onResolved) noexcept override;

                /// @private
                aws_host_resolver *GetUnderlyingHandle() noexcept override { return m_resolver; }
                /// @private
                aws_host_resolution_config *GetConfig() noexcept override { return &m_config; }

              private:
                aws_host_resolver *m_resolver;
                aws_host_resolution_config m_config;
                Allocator *m_allocator;
                bool m_initialized;
            };
        } // namespace Io
    }     // namespace Crt
} // namespace Aws
