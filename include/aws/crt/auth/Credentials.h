#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

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
            /**
             * A class to hold the basic components necessary for various AWS authentication protocols.
             */
            class AWS_CRT_CPP_API Credentials
            {
              public:
                Credentials(aws_credentials *credentials) noexcept;
                Credentials(
                    ByteCursor access_key_id,
                    ByteCursor secret_access_key,
                    ByteCursor session_token,
                    uint64_t expiration_timepoint_in_seconds,
                    Allocator *allocator = g_allocator) noexcept;

                ~Credentials();

                Credentials(const Credentials &) = delete;
                Credentials(Credentials &&) = delete;
                Credentials &operator=(const Credentials &) = delete;
                Credentials &operator=(Credentials &&) = delete;

                /**
                 * Gets the value of the access key component of aws credentials
                 */
                ByteCursor GetAccessKeyId() const noexcept;

                /**
                 * Gets the value of the secret access key component of aws credentials
                 */
                ByteCursor GetSecretAccessKey() const noexcept;

                /**
                 * Gets the value of the session token of aws credentials
                 */
                ByteCursor GetSessionToken() const noexcept;

                /**
                 * Gets the expiration timestamp for the credentials, or UINT64_MAX if no expiration
                 */
                uint64_t GetExpirationTimepointInSeconds() const noexcept;

                /**
                 * Validity check - returns true if the instance is valid, false otherwise
                 */
                explicit operator bool() const noexcept;

                /**
                 * Returns the underlying credentials implementation.
                 */
                aws_credentials *GetUnderlyingHandle() const noexcept { return m_credentials; }

              private:
                aws_credentials *m_credentials;
            };

            /**
             * Callback invoked by credentials providers when resolution succeeds (credentials will be non-null)
             * or fails (credentials will be null)
             */
            using OnCredentialsResolved = std::function<void(std::shared_ptr<Credentials>, int errorCode)>;

            /**
             * Base interface for all credentials providers.  Credentials providers are objects that
             * retrieve AWS credentials from some source.
             */
            class AWS_CRT_CPP_API ICredentialsProvider : public std::enable_shared_from_this<ICredentialsProvider>
            {
              public:
                virtual ~ICredentialsProvider() = default;

                /**
                 * Asynchronous method to query for AWS credentials based on the internal provider implementation.
                 */
                virtual bool GetCredentials(const OnCredentialsResolved &onCredentialsResolved) const = 0;

                /**
                 * Returns the underlying credentials provider implementation.  Support for credentials providers
                 * not based on a C implementation is theoretically possible, but requires some re-implementation to
                 * support provider chains and caching (whose implementations rely on links to C implementation
                 * providers)
                 */
                virtual aws_credentials_provider *GetUnderlyingHandle() const noexcept = 0;

                /**
                 * Validity check method
                 */
                virtual bool IsValid() const noexcept = 0;
            };

            /**
             * Configuration options for the static credentials provider
             */
            struct AWS_CRT_CPP_API CredentialsProviderStaticConfig
            {
                CredentialsProviderStaticConfig() : AccessKeyId{}, SecretAccessKey{}, SessionToken{} {}

                /**
                 * The value of the access key component for the provider's static aws credentials
                 */
                ByteCursor AccessKeyId;

                /**
                 * The value of the secret access key component for the provider's  static aws credentials
                 */
                ByteCursor SecretAccessKey;

                /**
                 * The value of the session token for the provider's  static aws credentials
                 */
                ByteCursor SessionToken;
            };

            /**
             * Configuration options for the profile credentials provider
             */
            struct AWS_CRT_CPP_API CredentialsProviderProfileConfig
            {
                CredentialsProviderProfileConfig()
                    : ProfileNameOverride{}, ConfigFileNameOverride{}, CredentialsFileNameOverride{}
                {
                }

                /**
                 * Override profile name to use (instead of default) when the provider sources credentials
                 */
                ByteCursor ProfileNameOverride;

                /**
                 * Override file path (instead of '~/.aws/config' for the aws config file to use during
                 * credential sourcing
                 */
                ByteCursor ConfigFileNameOverride;

                /**
                 * Override file path (instead of '~/.aws/credentials' for the aws credentials file to use during
                 * credential sourcing
                 */
                ByteCursor CredentialsFileNameOverride;
            };

            /**
             * Configuration options for the Ec2 instance metadata service credentials provider
             */
            struct AWS_CRT_CPP_API CredentialsProviderImdsConfig
            {
                CredentialsProviderImdsConfig() : Bootstrap(nullptr) {}

                /**
                 * Connection bootstrap to use to create the http connection required to
                 * query credentials from the Ec2 instance metadata service
                 */
                Io::ClientBootstrap *Bootstrap;
            };

            /**
             * Configuration options for a chain-of-responsibility-based credentials provider.
             * This provider works by traversing the chain and returning the first positive
             * result.
             */
            struct AWS_CRT_CPP_API CredentialsProviderChainConfig
            {
                CredentialsProviderChainConfig() : Providers() {}

                /**
                 * The sequence of providers that make up the chain.
                 */
                Vector<std::shared_ptr<ICredentialsProvider>> Providers;
            };

            /**
             * Configuration options for a provider that caches the results of another provider
             */
            struct AWS_CRT_CPP_API CredentialsProviderCachedConfig
            {
                CredentialsProviderCachedConfig() : Provider(), CachedCredentialTTL() {}

                /**
                 * The provider to cache credentials from
                 */
                std::shared_ptr<ICredentialsProvider> Provider;

                /**
                 * How long a cached credential set will be used for
                 */
                std::chrono::milliseconds CachedCredentialTTL;
            };

            /**
             * Configuration options for a provider that implements a cached provider chain
             * based on the AWS SDK defaults:
             *
             *   Cache-Of(Environment -> Profile -> IMDS)
             */
            struct AWS_CRT_CPP_API CredentialsProviderChainDefaultConfig
            {
                CredentialsProviderChainDefaultConfig() : Bootstrap(nullptr) {}

                /**
                 * Connection bootstrap to use to create the http connection required to
                 * query credentials from the Ec2 instance metadata service
                 */
                Io::ClientBootstrap *Bootstrap;
            };

            /**
             * Simple credentials provider implementation that wraps one of the internal C-based implementations.
             *
             * Contains a set of static factory methods for building each supported provider, as well as one for the
             * default provider chain.
             */
            class AWS_CRT_CPP_API CredentialsProvider : public ICredentialsProvider
            {
              public:
                CredentialsProvider(aws_credentials_provider *provider, Allocator *allocator = g_allocator) noexcept;

                virtual ~CredentialsProvider();

                CredentialsProvider(const CredentialsProvider &) = delete;
                CredentialsProvider(CredentialsProvider &&) = delete;
                CredentialsProvider &operator=(const CredentialsProvider &) = delete;
                CredentialsProvider &operator=(CredentialsProvider &&) = delete;

                /**
                 * Asynchronous method to query for AWS credentials based on the internal provider implementation.
                 */
                virtual bool GetCredentials(const OnCredentialsResolved &onCredentialsResolved) const override;

                /**
                 * Returns the underlying credentials provider implementation.
                 */
                virtual aws_credentials_provider *GetUnderlyingHandle() const noexcept override { return m_provider; }

                /**
                 * Validity check method
                 */
                virtual bool IsValid() const noexcept override { return m_provider != nullptr; }

                /*
                 * Factory methods for all of the basic credentials provider types
                 *
                 * NYI: X509, ECS
                 */

                /**
                 * Creates a provider that returns a fixed set of credentials
                 */
                static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderStatic(
                    const CredentialsProviderStaticConfig &config,
                    Allocator *allocator = g_allocator);

                /**
                 * Creates a provider that returns credentials sourced from environment variables
                 */
                static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderEnvironment(
                    Allocator *allocator = g_allocator);

                /**
                 * Creates a provider that returns credentials sourced from config files
                 */
                static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderProfile(
                    const CredentialsProviderProfileConfig &config,
                    Allocator *allocator = g_allocator);

                /**
                 * Creates a provider that returns credentials sourced from Ec2 instance metadata service
                 */
                static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderImds(
                    const CredentialsProviderImdsConfig &config,
                    Allocator *allocator = g_allocator);

                /**
                 * Creates a provider that sources credentials by querying a series of providers and
                 * returning the first valid credential set encountered
                 */
                static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderChain(
                    const CredentialsProviderChainConfig &config,
                    Allocator *allocator = g_allocator);

                /*
                 * Creates a provider that puts a simple time-based cache in front of its queries
                 * to a subordinate provider.
                 */
                static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderCached(
                    const CredentialsProviderCachedConfig &config,
                    Allocator *allocator = g_allocator);

                /**
                 * Creates the SDK-standard default credentials provider which is a cache-fronted chain of:
                 *
                 *   Environment -> Profile -> IMDS
                 *
                 */
                static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderChainDefault(
                    const CredentialsProviderChainDefaultConfig &config,
                    Allocator *allocator = g_allocator);

              private:
                static void s_onCredentialsResolved(aws_credentials *credentials, int error_code, void *user_data);

                Allocator *m_allocator;
                aws_credentials_provider *m_provider;
            };
        } // namespace Auth
    }     // namespace Crt
} // namespace Aws