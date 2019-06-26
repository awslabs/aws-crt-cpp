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

struct aws_credentials;
struct aws_credentials_provider;

namespace Aws
{
    namespace Crt
    {
        class AWS_CRT_CPP_API Credentials
        {
          public:
            Credentials(aws_credentials *credentials, Allocator *allocator) noexcept;
            Credentials(ByteCursor access_key_id, ByteCursor secret_access_key, ByteCursor session_token, Allocator *allocator) noexcept;

            ~Credentials();

            ByteCursor GetAccessKeyId(void) const noexcept;
            ByteCursor GetSecretAccessKey(void) const noexcept;
            ByteCursor GetSessionToken(void) const noexcept;

            aws_credentials *GetUnderlyingHandle(void) const noexcept;

          private:

            aws_credentials *m_credentials;
        };

        using OnCredentialsResolved = std::function<void(std::shared_ptr<Credentials>)>;

        class AWS_CRT_CPP_API ICredentialsProvider : public std::enable_shared_from_this<ICredentialsProvider> {
          public:
            virtual ~ICredentialsProvider() = default;

            virtual bool GetCredentials(const OnCredentialsResolved &onCredentialsResolved) const noexcept = 0;
        };

        struct CredentialsProviderStaticConfig {
          std::shared_ptr<Credentials> m_credentials;
        };

        struct CredentialsProviderProfileConfig {

        };

        struct CredentialsProviderImdsConfig {

        };

        struct CredentialsProviderChainConfig {

        };

        struct CredentialsProviderDefaultChainConfig {

        };

        class AWS_CRT_CPP_API WrappedCredentialsProvider : public ICredentialsProvider {
          public:

            WrappedCredentialsProvider(const WrappedCredentialsProvider &) = delete;
            WrappedCredentialsProvider(WrappedCredentialsProvider &&) = delete;
            WrappedCredentialsProvider &operator=(const WrappedCredentialsProvider &) = delete;
            WrappedCredentialsProvider &operator=(WrappedCredentialsProvider &&) = delete;

            virtual ~WrappedCredentialsProvider();

            virtual bool GetCredentials(const OnCredentialsResolved &onCredentialsResolved) const noexcept override;

            static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderStatic(const CredentialsProviderStaticConfig &config, Allocator *allocator);
            static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderEnvironment(Allocator *allocator);
            static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderProfile(const CredentialsProviderProfileConfig &config, Allocator *allocator);
            static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderImds(const CredentialsProviderImdsConfig &config, Allocator *allocator);
            static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderChain(const CredentialsProviderChainConfig &config, Allocator *allocator);
            static std::shared_ptr<ICredentialsProvider> CreateCredentialsProviderDefaultChain(const CredentialsProviderDefaultChainConfig &config, Allocator *allocator);

          protected:

            WrappedCredentialsProvider(aws_credentials_provider *provider, Allocator *allocator) noexcept;

          private:

            static void s_onCredentialsResolved(aws_credentials *credentials, void *user_data) noexcept;

            Allocator *m_allocator;
            aws_credentials_provider *m_provider;
        };

    }
}