#pragma once
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

#include <aws/crt/ByteBuf.h>
#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>

#include <chrono>
#include <functional>

struct aws_credentials;
struct aws_credentials_provider;

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            class ClientBootstrap;
        }

        namespace Auth
        {
            /*
             * A class to hold the basic components necessary for various AWS authentication protocols.
             */
            class AWS_CRT_CPP_API Credentials
            {
              public:
                Credentials(aws_credentials *credentials, Allocator *allocator = DefaultAllocator()) noexcept;
                Credentials(
                    ByteCursor access_key_id,
                    ByteCursor secret_access_key,
                    ByteCursor session_token,
                    Allocator *allocator = DefaultAllocator()) noexcept;

                ~Credentials();

                Credentials(const Credentials &) = delete;
                Credentials(Credentials &&) = delete;
                Credentials &operator=(const Credentials &) = delete;
                Credentials &operator=(Credentials &&) = delete;

                ByteCursor GetAccessKeyId() const noexcept;

                ByteCursor GetSecretAccessKey() const noexcept;

                ByteCursor GetSessionToken() const noexcept;

                operator bool() const noexcept;

                aws_credentials *GetUnderlyingHandle() const noexcept;

              private:
                aws_credentials *m_credentials;
            };

            /*
             * Callback invoked by credentials providers when resolution succeeds (credentials will be non-null)
             * or fails (credentials will be null)
             */
            using OnCredentialsResolved = std::function<void(std::shared_ptr<Credentials>)>;

            /*
             * Base interface for all credentials providers.  Credentials providers are objects that
             * retrieve (asynchronously) AWS credentials from some source.
             */
            class AWS_CRT_CPP_API ICredentialsProvider : public std::enable_shared_from_this<ICredentialsProvider>
            {
              public:
                virtual ~ICredentialsProvider() = default;

                /*
                 * Asynchronous method to query for AWS credentials based on the internal provider implementation.
                 */
                virtual bool GetCredentials(const OnCredentialsResolved &onCredentialsResolved) const = 0;

                /*
                 * Returns the underlying credentials provider implementation.  Support for credentials providers
                 * not based on a C implementation is theoretically possible, but requires some re-implementation to
                 * support provider chains and caching (whose implementations rely on links to C implementation
                 * providers)
                 */
                virtual aws_credentials_provider *GetUnderlyingHandle() const noexcept = 0;

                /*
                 * Validity check
                 */
                virtual operator bool() const noexcept = 0;
            };

            /*
             * Configuration options for the static credentials provider
             */
            struct AWS_CRT_CPP_API CredentialsProviderStaticConfig
            {
                CredentialsProviderStaticConfig() : m_accessKeyId(), m_secretAccessKey(), m_sessionToken() {}

                ByteCursor m_accessKeyId;
                ByteCursor m_secretAccessKey;
                ByteCursor m_sessionToken;
            };

            /*
             * Configuration options for the profile credentials provider
             */
            struct AWS_CRT_CPP_API CredentialsProviderProfileConfig
            {
                CredentialsProviderProfileConfig()
                    : m_profileNameOverride(), m_configFileNameOverride(), m_credentialsFileNameOverride()
                {
                }

                ByteCursor m_profileNameOverride;
                ByteCursor m_configFileNameOverride;
                ByteCursor m_credentialsFileNameOverride;
            };

            /*
             * Configuration options for the Ec2 instance metadata service credentials provider
             */
            struct AWS_CRT_CPP_API CredentialsProviderImdsConfig
            {
                CredentialsProviderImdsConfig() : m_bootstrap(nullptr) {}

                Io::ClientBootstrap *m_bootstrap;
            };

            /*
             * Configuration options for a chain-of-responsibility-based credentials provider.
             * This provider works by traversing the chain and returning the first positive
             * result.
             */
            struct AWS_CRT_CPP_API CredentialsProviderChainConfig
            {
                CredentialsProviderChainConfig() : m_providers() {}

                Vector<std::shared_ptr<ICredentialsProvider>> m_providers;
            };

            /*
             * Configuration options for a provider that caches the results of another provider
             */
            struct AWS_CRT_CPP_API CredentialsProviderCachedConfig
            {
                CredentialsProviderCachedConfig() : m_provider(nullptr), m_refreshTime() {}

                std::shared_ptr<ICredentialsProvider> m_provider;
                std::chrono::milliseconds m_refreshTime;
            };

            /*
             * Configuration options for a provider that implements a cached provider chain
             * based on the AWS SDK defaults:
             *
             *   Cache-Of(Environment -> Profile -> IMDS)
             */
            struct AWS_CRT_CPP_API CredentialsProviderChainDefaultConfig
            {
                CredentialsProviderChainDefaultConfig() : m_bootstrap(nullptr) {}

                Io::ClientBootstrap *m_bootstrap;
            };

            /*
             * Simple credentials provider implementation that wraps one of the internal C-based implementations.
             *
             * Contains a set of static factory methods for building each supported provider, as well as one for the
             * default provider chain.
             */
            class AWS_CRT_CPP_API CredentialsProvider : public ICredentialsProvider
            {
              public:
                CredentialsProvider(
                    aws_credentials_provider *provider,
                    Allocator *allocator = DefaultAllocator()) noexcept;

                virtual ~CredentialsProvider();

                CredentialsProvider(const CredentialsProvider &) = delete;
                CredentialsProvider(CredentialsProvider &&) = delete;
                CredentialsProvider &operator=(const CredentialsProvider &) = delete;
                CredentialsProvider &operator=(CredentialsProvider &&) = delete;

                /*
                 * Asynchronous method to query for AWS credentials based on the internal provider implementation.
                 */
                virtual bool GetCredentials(const OnCredentialsResolved &onCredentialsResolved) const override;

                virtual aws_credentials_provider *GetUnderlyingHandle() const noexcept override { return m_provider; }

                virtual operator bool() const noexcept override { return m_provider != nullptr; }

                /*
                 * Factory methods for all of the basic credentials provider types
                 *
                 * NYI: X509, ECS
                 */

                /**
                 * A provider that returns a fixed set of credentials
                 */
                static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderStatic(
                    const CredentialsProviderStaticConfig &config,
                    Allocator *allocator = DefaultAllocator());

                /*
                 * A provider that returns credentials sourced from environment variables
                 */
                static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderEnvironment(
                    Allocator *allocator = DefaultAllocator());

                /*
                 * A provider that returns credentials sourced from config files
                 */
                static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderProfile(
                    const CredentialsProviderProfileConfig &config,
                    Allocator *allocator = DefaultAllocator());

                /*
                 * A provider that returns credentials sourced from Ec2 instance metadata service
                 */
                static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderImds(
                    const CredentialsProviderImdsConfig &config,
                    Allocator *allocator = DefaultAllocator());

                /*
                 * A provider that sources credentials by querying a series of providers and
                 * returning the first valid credential set encountered
                 */
                static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderChain(
                    const CredentialsProviderChainConfig &config,
                    Allocator *allocator = DefaultAllocator());

                /*
                 * A provider that puts a simple time-based cache in front of its queries
                 * to a subordinate provider.
                 */
                static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderCached(
                    const CredentialsProviderCachedConfig &config,
                    Allocator *allocator = DefaultAllocator());

                /*
                 * The SDK-standard default credentials provider which is a cache-fronted chain of:
                 *
                 *   Environment -> Profile -> IMDS
                 *
                 */
                static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderChainDefault(
                    const CredentialsProviderChainDefaultConfig &config,
                    Allocator *allocator = DefaultAllocator());

              private:
                static void s_onCredentialsResolved(aws_credentials *credentials, void *user_data);

                Allocator *m_allocator;
                aws_credentials_provider *m_provider;
            };
        } // namespace Auth
    }     // namespace Crt
} // namespace Aws