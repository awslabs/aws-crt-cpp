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

#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>

#include <chrono>

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

        /*
         * A class to hold the basic components necessary for various AWS authentication protocols.
         */
        class AWS_CRT_CPP_API Credentials
        {
          public:
            Credentials(aws_credentials *credentials, Allocator *allocator = DefaultAllocator()) noexcept;
            Credentials(ByteCursor access_key_id, ByteCursor secret_access_key, ByteCursor session_token, Allocator *allocator = DefaultAllocator()) noexcept;

            ~Credentials();

            ByteCursor GetAccessKeyId(void) const noexcept;
            ByteCursor GetSecretAccessKey(void) const noexcept;
            ByteCursor GetSessionToken(void) const noexcept;

            aws_credentials *GetUnderlyingHandle(void) const noexcept;

          private:

            aws_credentials *m_credentials;
        };

        /*
         * Callback invoked by credentials providers when resolution succeeds (credentials will be non-null)
         * or fails (credentials will be null)
         */
        using OnCredentialsResolved = std::function<void(std::shared_ptr<Credentials>)>;

        /*
         * Simple base interface for credentials providers.  Credentials providers are objects that
         * retrieve (asynchronously) AWS credentials from some source.
         */
        class AWS_CRT_CPP_API ICredentialsProvider : public std::enable_shared_from_this<ICredentialsProvider> {
          public:
            virtual ~ICredentialsProvider() = default;

            /*
             * Asynchronous method to query for credentials based on the internal provider implementation.
             */
            virtual bool GetCredentials(const OnCredentialsResolved &onCredentialsResolved) const = 0;

            virtual aws_credentials_provider *GetUnderlyingHandle(void) const noexcept = 0;
        };

        /*
         * Configuration options for the static credentials provider
         */
        struct CredentialsProviderStaticConfig {
            ByteCursor m_accessKeyId;
            ByteCursor m_secretAccessKey;
            ByteCursor m_sessionToken;
        };

        /*
         * Configuration options for the profile credentials provider
         */
        struct CredentialsProviderProfileConfig {
            ByteCursor m_profileNameOverride;
            ByteCursor m_configFileNameOverride;
            ByteCursor m_credentialsFileNameOverride;
        };

        /*
         * Configuration options for the Ec2 instance metadata service credentials provider
         */
        struct CredentialsProviderImdsConfig {
            Io::ClientBootstrap *m_bootstrap;
        };

        /*
         * Configuration options for a chain-of-responsibility-based credentials provider.
         * This provider works by traversing the chain and returning the first positive
         * result.
         */
        struct CredentialsProviderChainConfig {
            Vector<std::shared_ptr<ICredentialsProvider>> m_providers;
        };

        /*
         * Configuration options for a provider that caches the results of another provider
         */
        struct CredentialsProviderCachedConfig {
            std::shared_ptr<ICredentialsProvider> m_provider;
            std::chrono::milliseconds m_refreshTime;
        };

        /*
         * Configuration options for a provider that implements a cached provider chain
         * based on the AWS SDK defaults:
         *
         *   Cache-Of(Environment -> Profile -> IMDS)
         */
        struct CredentialsProviderChainDefaultConfig {
            Io::ClientBootstrap *m_bootstrap;
        };

        /*
         * Simple credentials provider implementation that wraps one of the internal C-based implementations.
         *
         * Contains a set of factory methods for building each supported provider, as well as one for the
         * default provider chain.
         */
        class AWS_CRT_CPP_API CredentialsProvider : public ICredentialsProvider {
          public:

            CredentialsProvider(aws_credentials_provider *provider, Allocator *allocator = DefaultAllocator()) noexcept;

            CredentialsProvider(const CredentialsProvider &) = delete;
            CredentialsProvider(CredentialsProvider &&) = delete;
            CredentialsProvider &operator=(const CredentialsProvider &) = delete;
            CredentialsProvider &operator=(CredentialsProvider &&) = delete;

            virtual ~CredentialsProvider();

            virtual bool GetCredentials(const OnCredentialsResolved &onCredentialsResolved) const override;

            virtual aws_credentials_provider *GetUnderlyingHandle(void) const noexcept override { return m_provider; }

            static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderStatic(const CredentialsProviderStaticConfig &config, Allocator *allocator = DefaultAllocator());
            static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderEnvironment(Allocator *allocator = DefaultAllocator());
            static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderProfile(const CredentialsProviderProfileConfig &config, Allocator *allocator = DefaultAllocator());
            static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderImds(const CredentialsProviderImdsConfig &config, Allocator *allocator = DefaultAllocator());
            static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderChain(const CredentialsProviderChainConfig &config, Allocator *allocator = DefaultAllocator());
            static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderCached(const CredentialsProviderCachedConfig &config, Allocator *allocator = DefaultAllocator());
            static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderChainDefault(
                const CredentialsProviderChainDefaultConfig &config, Allocator *allocator = DefaultAllocator());

          private:

            static void s_onCredentialsResolved(aws_credentials *credentials, void *user_data);

            Allocator *m_allocator;
            aws_credentials_provider *m_provider;
        };

    }
}