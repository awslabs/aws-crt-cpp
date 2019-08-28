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

#include <aws/crt/auth/Credentials.h>

#include <aws/crt/io/Bootstrap.h>

#include <aws/auth/credentials.h>
#include <aws/common/string.h>

#include <algorithm>

namespace Aws
{
    namespace Crt
    {
        namespace Auth
        {
            Credentials::Credentials(aws_credentials *credentials, Allocator *allocator) noexcept
                : m_credentials(aws_credentials_new_copy(allocator, credentials))
            {
            }

            Credentials::Credentials(
                ByteCursor access_key_id,
                ByteCursor secret_access_key,
                ByteCursor session_token,
                Allocator *allocator) noexcept
                : m_credentials(
                      aws_credentials_new_from_cursors(allocator, &access_key_id, &secret_access_key, &session_token))
            {
            }

            Credentials::~Credentials()
            {
                aws_credentials_destroy(m_credentials);
                m_credentials = nullptr;
            }

            ByteCursor Credentials::GetAccessKeyId() const noexcept
            {
                if (m_credentials)
                {
                    return aws_byte_cursor_from_string(m_credentials->access_key_id);
                }
                else
                {
                    return ByteCursor{0, nullptr};
                }
            }

            ByteCursor Credentials::GetSecretAccessKey() const noexcept
            {
                if (m_credentials)
                {
                    return aws_byte_cursor_from_string(m_credentials->secret_access_key);
                }
                else
                {
                    return ByteCursor{0, nullptr};
                }
            }

            ByteCursor Credentials::GetSessionToken() const noexcept
            {
                if (m_credentials)
                {
                    return aws_byte_cursor_from_string(m_credentials->session_token);
                }
                else
                {
                    return ByteCursor{0, nullptr};
                }
            }

            Credentials::operator bool() const noexcept { return m_credentials != nullptr; }

            CredentialsProvider::CredentialsProvider(aws_credentials_provider *provider, Allocator *allocator) noexcept
                : m_allocator(allocator), m_provider(provider)
            {
            }

            CredentialsProvider::~CredentialsProvider()
            {
                if (m_provider != nullptr)
                {
                    aws_credentials_provider_release(m_provider);
                    m_provider = nullptr;
                }
            }

            struct CredentialsProviderCallbackArgs
            {
                CredentialsProviderCallbackArgs() = default;

                OnCredentialsResolved m_onCredentialsResolved;
                std::shared_ptr<const CredentialsProvider> m_provider;
            };

            void CredentialsProvider::s_onCredentialsResolved(aws_credentials *credentials, void *user_data)
            {
                CredentialsProviderCallbackArgs *callbackArgs =
                    static_cast<CredentialsProviderCallbackArgs *>(user_data);

                auto credentialsPtr = std::make_shared<Credentials>(credentials, callbackArgs->m_provider->m_allocator);

                callbackArgs->m_onCredentialsResolved(credentialsPtr);

                Aws::Crt::Delete(callbackArgs, callbackArgs->m_provider->m_allocator);
            }

            bool CredentialsProvider::GetCredentials(const OnCredentialsResolved &onCredentialsResolved) const
            {
                if (m_provider == nullptr)
                {
                    return false;
                }

                auto callbackArgs = Aws::Crt::New<CredentialsProviderCallbackArgs>(m_allocator);
                if (callbackArgs == nullptr)
                {
                    return false;
                }

                callbackArgs->m_provider = std::static_pointer_cast<const CredentialsProvider>(shared_from_this());
                callbackArgs->m_onCredentialsResolved = onCredentialsResolved;

                aws_credentials_provider_get_credentials(m_provider, s_onCredentialsResolved, callbackArgs);

                return true;
            }

            static std::shared_ptr<ICredentialsProvider> s_CreateWrappedProvider(
                struct aws_credentials_provider *raw_provider,
                Allocator *allocator)
            {
                if (raw_provider == nullptr)
                {
                    return nullptr;
                }

                /* Switch to some kind of make_shared/allocate_shared when allocator support improves */
                auto provider = Aws::Crt::MakeShared<CredentialsProvider>(allocator, raw_provider, allocator);

                return std::static_pointer_cast<ICredentialsProvider>(provider);
            }

            std::shared_ptr<ICredentialsProvider> CredentialsProvider::CreateCredentialsProviderStatic(
                const CredentialsProviderStaticConfig &config,
                Allocator *allocator)
            {
                return s_CreateWrappedProvider(
                    aws_credentials_provider_new_static(
                        allocator, config.AccessKeyId, config.SecretAccessKey, config.SessionToken),
                    allocator);
            }

            std::shared_ptr<ICredentialsProvider> CredentialsProvider::CreateCredentialsProviderEnvironment(
                Allocator *allocator)
            {
                return s_CreateWrappedProvider(aws_credentials_provider_new_environment(allocator), allocator);
            }

            std::shared_ptr<ICredentialsProvider> CredentialsProvider::CreateCredentialsProviderProfile(
                const CredentialsProviderProfileConfig &config,
                Allocator *allocator)
            {
                struct aws_credentials_provider_profile_options raw_config;
                AWS_ZERO_STRUCT(raw_config);

                raw_config.config_file_name_override = config.ConfigFileNameOverride;
                raw_config.credentials_file_name_override = config.CredentialsFileNameOverride;
                raw_config.profile_name_override = config.ProfileNameOverride;

                return s_CreateWrappedProvider(aws_credentials_provider_new_profile(allocator, &raw_config), allocator);
            }

            std::shared_ptr<ICredentialsProvider> CredentialsProvider::CreateCredentialsProviderImds(
                const CredentialsProviderImdsConfig &config,
                Allocator *allocator)
            {
                struct aws_credentials_provider_imds_options raw_config;
                AWS_ZERO_STRUCT(raw_config);

                raw_config.bootstrap = config.Bootstrap->GetUnderlyingHandle();

                return s_CreateWrappedProvider(aws_credentials_provider_new_imds(allocator, &raw_config), allocator);
            }

            std::shared_ptr<ICredentialsProvider> CredentialsProvider::CreateCredentialsProviderChain(
                const CredentialsProviderChainConfig &config,
                Allocator *allocator)
            {
                Vector<aws_credentials_provider *> providers;
                providers.reserve(config.Providers.size());

                std::for_each(
                    config.Providers.begin(),
                    config.Providers.end(),
                    [&](const std::shared_ptr<ICredentialsProvider> &provider) {
                        providers.push_back(provider->GetUnderlyingHandle());
                    });

                struct aws_credentials_provider_chain_options raw_config;
                AWS_ZERO_STRUCT(raw_config);

                raw_config.providers = providers.data();
                raw_config.provider_count = config.Providers.size();

                return s_CreateWrappedProvider(aws_credentials_provider_new_chain(allocator, &raw_config), allocator);
            }

            std::shared_ptr<ICredentialsProvider> CredentialsProvider::CreateCredentialsProviderCached(
                const CredentialsProviderCachedConfig &config,
                Allocator *allocator)
            {
                struct aws_credentials_provider_cached_options raw_config;
                AWS_ZERO_STRUCT(raw_config);

                raw_config.source = config.Provider->GetUnderlyingHandle();
                raw_config.refresh_time_in_milliseconds = config.CachedCredentialTTL.count();

                return s_CreateWrappedProvider(aws_credentials_provider_new_cached(allocator, &raw_config), allocator);
            }

            std::shared_ptr<ICredentialsProvider> CredentialsProvider::CreateCredentialsProviderChainDefault(
                const CredentialsProviderChainDefaultConfig &config,
                Allocator *allocator)
            {
                struct aws_credentials_provider_chain_default_options raw_config;
                AWS_ZERO_STRUCT(raw_config);

                raw_config.bootstrap = config.Bootstrap->GetUnderlyingHandle();

                return s_CreateWrappedProvider(
                    aws_credentials_provider_new_chain_default(allocator, &raw_config), allocator);
            }
        } // namespace Auth
    }     // namespace Crt
} // namespace Aws