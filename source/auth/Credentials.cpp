/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/auth/Credentials.h>

#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/http/HttpProxyStrategy.h>

#include <aws/auth/credentials.h>
#include <aws/common/string.h>

#include <algorithm>
#include <aws/http/connection.h>

namespace Aws
{
    namespace Crt
    {
        namespace Auth
        {
            Credentials::Credentials(const aws_credentials *credentials) noexcept : m_credentials(credentials)
            {
                if (credentials != nullptr)
                {
                    aws_credentials_acquire(credentials);
                }
            }

            Credentials::Credentials(
                ByteCursor access_key_id,
                ByteCursor secret_access_key,
                ByteCursor session_token,
                uint64_t expiration_timepoint_in_seconds,
                Allocator *allocator) noexcept
                : m_credentials(aws_credentials_new(
                      allocator,
                      access_key_id,
                      secret_access_key,
                      session_token,
                      expiration_timepoint_in_seconds))
            {
            }

            Credentials::~Credentials()
            {
                aws_credentials_release(m_credentials);
                m_credentials = nullptr;
            }

            ByteCursor Credentials::GetAccessKeyId() const noexcept
            {
                if (m_credentials)
                {
                    return aws_credentials_get_access_key_id(m_credentials);
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
                    return aws_credentials_get_secret_access_key(m_credentials);
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
                    return aws_credentials_get_session_token(m_credentials);
                }
                else
                {
                    return ByteCursor{0, nullptr};
                }
            }

            uint64_t Credentials::GetExpirationTimepointInSeconds() const noexcept
            {
                if (m_credentials)
                {
                    return aws_credentials_get_expiration_timepoint_seconds(m_credentials);
                }
                else
                {
                    return 0;
                }
            }

            Credentials::operator bool() const noexcept { return m_credentials != nullptr; }

            CredentialsProvider::CredentialsProvider(aws_credentials_provider *provider, Allocator *allocator) noexcept
                : m_allocator(allocator), m_provider(provider)
            {
            }

            CredentialsProvider::~CredentialsProvider()
            {
                if (m_provider)
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

            void CredentialsProvider::s_onCredentialsResolved(
                aws_credentials *credentials,
                int error_code,
                void *user_data)
            {
                CredentialsProviderCallbackArgs *callbackArgs =
                    static_cast<CredentialsProviderCallbackArgs *>(user_data);

                auto credentialsPtr =
                    Aws::Crt::MakeShared<Credentials>(callbackArgs->m_provider->m_allocator, credentials);

                callbackArgs->m_onCredentialsResolved(credentialsPtr, error_code);

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
                aws_credentials_provider_static_options staticOptions;
                AWS_ZERO_STRUCT(staticOptions);
                staticOptions.access_key_id = config.AccessKeyId;
                staticOptions.secret_access_key = config.SecretAccessKey;
                staticOptions.session_token = config.SessionToken;
                return s_CreateWrappedProvider(
                    aws_credentials_provider_new_static(allocator, &staticOptions), allocator);
            }

            std::shared_ptr<ICredentialsProvider> CredentialsProvider::CreateCredentialsProviderEnvironment(
                Allocator *allocator)
            {
                aws_credentials_provider_environment_options environmentOptions;
                AWS_ZERO_STRUCT(environmentOptions);
                return s_CreateWrappedProvider(
                    aws_credentials_provider_new_environment(allocator, &environmentOptions), allocator);
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
                raw_config.bootstrap = config.Bootstrap ? config.Bootstrap->GetUnderlyingHandle() : nullptr;
                raw_config.tls_ctx = config.TlsContext ? config.TlsContext->GetUnderlyingHandle() : nullptr;

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
                raw_config.tls_ctx = config.TlsContext ? config.TlsContext->GetUnderlyingHandle() : nullptr;

                return s_CreateWrappedProvider(
                    aws_credentials_provider_new_chain_default(allocator, &raw_config), allocator);
            }

            std::shared_ptr<ICredentialsProvider> CredentialsProvider::CreateCredentialsProviderX509(
                const CredentialsProviderX509Config &config,
                Allocator *allocator)
            {
                struct aws_credentials_provider_x509_options raw_config;
                AWS_ZERO_STRUCT(raw_config);

                raw_config.bootstrap = config.Bootstrap->GetUnderlyingHandle();
                raw_config.tls_connection_options = config.TlsOptions.GetUnderlyingHandle();
                raw_config.thing_name = aws_byte_cursor_from_c_str(config.ThingName.c_str());
                raw_config.role_alias = aws_byte_cursor_from_c_str(config.RoleAlias.c_str());
                raw_config.endpoint = aws_byte_cursor_from_c_str(config.Endpoint.c_str());

                struct aws_http_proxy_options proxy_options;
                AWS_ZERO_STRUCT(proxy_options);
                if (config.ProxyOptions.has_value())
                {
                    const Http::HttpClientConnectionProxyOptions &proxy_config = config.ProxyOptions.value();
                    proxy_config.InitializeRawProxyOptions(proxy_options);

                    raw_config.proxy_options = &proxy_options;
                }

                return s_CreateWrappedProvider(aws_credentials_provider_new_x509(allocator, &raw_config), allocator);
            }

            struct DelegateCredentialsProviderCallbackArgs
            {
                DelegateCredentialsProviderCallbackArgs() = default;

                Allocator *allocator;
                GetCredentialsHandler m_Handler;
            };

            static int s_onDelegateGetCredentials(
                void *delegate_user_data,
                aws_on_get_credentials_callback_fn callback,
                void *callback_user_data)
            {
                auto args = static_cast<DelegateCredentialsProviderCallbackArgs *>(delegate_user_data);
                auto creds = args->m_Handler();
                struct aws_credentials *m_credentials = (struct aws_credentials *)(void *)creds->GetUnderlyingHandle();
                callback(m_credentials, AWS_ERROR_SUCCESS, callback_user_data);
                return AWS_OP_SUCCESS;
            }

            static void s_onDelegateShutdownComplete(void *user_data)
            {
                auto args = static_cast<DelegateCredentialsProviderCallbackArgs *>(user_data);
                Aws::Crt::Delete(args, args->allocator);
            }

            std::shared_ptr<ICredentialsProvider> CredentialsProvider::CreateCredentialsProviderDelegate(
                const CredentialsProviderDelegateConfig &config,
                Allocator *allocator)
            {
                struct aws_credentials_provider_delegate_options raw_config;
                AWS_ZERO_STRUCT(raw_config);

                auto delegateCallbackArgs = Aws::Crt::New<DelegateCredentialsProviderCallbackArgs>(allocator);
                delegateCallbackArgs->allocator = allocator;
                delegateCallbackArgs->m_Handler = config.Handler;
                raw_config.delegate_user_data = delegateCallbackArgs;
                raw_config.get_credentials = s_onDelegateGetCredentials;
                aws_credentials_provider_shutdown_options options;
                options.shutdown_callback = s_onDelegateShutdownComplete;
                options.shutdown_user_data = delegateCallbackArgs;
                raw_config.shutdown_options = options;
                return s_CreateWrappedProvider(
                    aws_credentials_provider_new_delegate(allocator, &raw_config), allocator);
            }

        } // namespace Auth
    }     // namespace Crt
} // namespace Aws
