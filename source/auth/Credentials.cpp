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

#include <aws/auth/credentials.h>
#include <aws/common/string.h>

namespace Aws
{
    namespace Crt
    {
        Credentials::Credentials(struct aws_credentials *credentials, Allocator *allocator) noexcept :
            m_credentials(aws_credentials_new_copy(allocator, credentials)) {}

        Credentials::Credentials(ByteCursor access_key_id, ByteCursor secret_access_key, ByteCursor session_token, Allocator *allocator) noexcept :
            m_credentials(aws_credentials_new_from_cursors(allocator, &access_key_id, &secret_access_key, &session_token))
        {}

        Credentials::~Credentials() {
            aws_credentials_destroy(m_credentials);
        }

        ByteCursor Credentials::GetAccessKeyId(void) const noexcept
        {
            return aws_byte_cursor_from_string(m_credentials->access_key_id);
        }

        ByteCursor Credentials::GetSecretAccessKey(void) const noexcept
        {
            return aws_byte_cursor_from_string(m_credentials->secret_access_key);
        }

        ByteCursor Credentials::GetSessionToken(void) const noexcept
        {
            return aws_byte_cursor_from_string(m_credentials->session_token);
        }

        WrappedCredentialsProvider::WrappedCredentialsProvider(struct aws_credentials_provider *provider, Allocator *allocator) noexcept :
            m_allocator(allocator),
            m_provider(provider)
        {}

        WrappedCredentialsProvider::~WrappedCredentialsProvider()
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
          std::shared_ptr<const WrappedCredentialsProvider> m_provider;
        };

        void WrappedCredentialsProvider::s_onCredentialsResolved(struct aws_credentials *credentials, void *user_data) noexcept {
            CredentialsProviderCallbackArgs *callbackArgs = static_cast<CredentialsProviderCallbackArgs *>(user_data);

            auto credentialsPtr = std::make_shared<Credentials>(credentials, callbackArgs->m_provider->m_allocator);

            callbackArgs->m_onCredentialsResolved(credentialsPtr);

            Aws::Crt::Delete(callbackArgs, callbackArgs->m_provider->m_allocator);
        }

        bool WrappedCredentialsProvider::GetCredentials(const OnCredentialsResolved &onCredentialsResolved) const noexcept
        {
            auto callbackArgs = Aws::Crt::New<CredentialsProviderCallbackArgs>(m_allocator);
            if (callbackArgs == nullptr) {
                return false;
            }

            callbackArgs->m_provider = std::static_pointer_cast<const WrappedCredentialsProvider>(shared_from_this());
            callbackArgs->m_onCredentialsResolved = onCredentialsResolved;

            aws_credentials_provider_get_credentials(m_provider, s_onCredentialsResolved, callbackArgs);

            return true;
        }

        std::shared_ptr<ICredentialsProvider> WrappedCredentialsProvider::CreateCredentialsProviderStatic(const CredentialsProviderStaticConfig &config, Allocator *allocator)
        {
            return nullptr;
        }

        std::shared_ptr<ICredentialsProvider> WrappedCredentialsProvider::CreateCredentialsProviderEnvironment(Allocator *allocator)
        {
            return nullptr;
        }

        std::shared_ptr<ICredentialsProvider> WrappedCredentialsProvider::CreateCredentialsProviderProfile(const CredentialsProviderProfileConfig &config, Allocator *allocator)
        {
            return nullptr;
        }

        std::shared_ptr<ICredentialsProvider> WrappedCredentialsProvider::CreateCredentialsProviderImds(const CredentialsProviderImdsConfig &config, Allocator *allocator)
        {
            return nullptr;
        }

        std::shared_ptr<ICredentialsProvider> WrappedCredentialsProvider::CreateCredentialsProviderChain(const CredentialsProviderChainConfig &config, Allocator *allocator)
        {
            return nullptr;
        }

        std::shared_ptr<ICredentialsProvider> WrappedCredentialsProvider::CreateCredentialsProviderDefaultChain(const CredentialsProviderDefaultChainConfig &config, Allocator *allocator)
        {
            return nullptr;
        }

    }
}