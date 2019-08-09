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

#include <aws/crt/DateTime.h>
#include <aws/crt/Types.h>
#include <aws/crt/auth/Signing.h>

struct aws_signer;
struct aws_signing_config_aws;

namespace Aws
{
    namespace Crt
    {
        class ByteCursor;
        class ByteBuf;

        namespace Auth
        {
            class Credentials;
            class ICredentialsProvider;

            enum class SigningAlgorithm
            {
                SigV4Header = AWS_SIGNING_ALGORITHM_SIG_V4_HEADER,
                SigV4QueryParam = AWS_SIGNING_ALGORITHM_SIG_V4_QUERY_PARAM,

                Count = AWS_SIGNING_ALGORITHM_COUNT
            };

            /*
             * Wrapper around the configuration structure specific to the Aws
             * Sigv4 signing process
             */
            class AWS_CRT_CPP_API AwsSigningConfig : public ISigningConfig
            {
              public:
                AwsSigningConfig(Allocator *allocator = DefaultAllocator());
                virtual ~AwsSigningConfig();

                virtual SigningConfigType GetType(void) const noexcept override { return SigningConfigType::Aws; }

                /*
                 * Credentials to sign the request with
                 */
                std::shared_ptr<Credentials> GetCredentials() const noexcept;
                void SetCredentials(const std::shared_ptr<Credentials> &credentials) noexcept;

                /*
                 * What signing process do we want to invoke
                 */
                SigningAlgorithm GetSigningAlgorithm() const noexcept;
                void SetSigningAlgorithm(SigningAlgorithm algorithm) noexcept;

                /*
                 * The region to sign against
                 */
                ByteCursor GetRegion() const noexcept;
                void SetRegion(ByteCursor region) noexcept;

                /*
                 * name of service to sign a request for
                 */
                ByteCursor GetService() const noexcept;
                void SetService(ByteCursor service) noexcept;

                /*
                 * Timestamp to use during the signing process.
                 */
                DateTime GetDate() const noexcept;
                void SetDate(const DateTime &date) noexcept;

                /*
                 * We assume the uri will be encoded once in preparation for transmission.  Certain services
                 * do not decode before checking signature, requiring us to actually double-encode the uri in the
                 * canonical request in order to pass a signature check.
                 */
                bool GetUseDoubleUriEncode() const noexcept;
                void SetUseDoubleUriEncode(bool useDoubleUriEncode) noexcept;

                /*
                 * Controls whether or not the uri paths should be normalized when building the canonical request
                 */
                bool GetShouldNormalizeUriPath() const noexcept;
                void SetShouldNormalizeUriPath(bool shouldNormalizeUriPath) noexcept;

                /*
                 * If true adds the x-amz-content-sha256 header (with appropriate value) to the canonical request,
                 * otherwise does nothing
                 */
                bool GetSignBody() const noexcept;
                void SetSignBody(bool signBody) noexcept;

              private:
                Allocator *m_allocator;

                std::shared_ptr<Credentials> m_credentials;

                struct aws_signing_config_aws *m_config;
            };

            /*
             * Http request signer that wraps any aws-c-* signer implementation
             */
            class AWS_CRT_CPP_API AwsHttpRequestSigner : public IHttpRequestSigner
            {
              public:
                AwsHttpRequestSigner(aws_signer *signer, Allocator *allocator = DefaultAllocator());
                virtual ~AwsHttpRequestSigner();

                virtual operator bool() const override { return m_signer != nullptr; }

              protected:
                Allocator *m_allocator;

                aws_signer *m_signer;
            };

            /*
             * Http request signer that performs Aws Sigv4 signing
             */
            class AWS_CRT_CPP_API Sigv4HttpRequestSigner : public AwsHttpRequestSigner
            {
              public:
                Sigv4HttpRequestSigner(Allocator *allocator = DefaultAllocator());
                virtual ~Sigv4HttpRequestSigner() = default;

                virtual bool SignRequest(Aws::Crt::Http::HttpRequest &request, const ISigningConfig *config) override;
            };

            /*
             * Signing pipeline that performs Aws Sigv4 signing with credentials sourced from
             * an internally referenced credentials provider
             */
            class AWS_CRT_CPP_API Sigv4HttpRequestSigningPipeline : public IHttpRequestSigningPipeline
            {
              public:
                Sigv4HttpRequestSigningPipeline(
                    const std::shared_ptr<ICredentialsProvider> &credentialsProvider,
                    Allocator *allocator = DefaultAllocator());

                virtual ~Sigv4HttpRequestSigningPipeline();

                virtual void SignRequest(
                    const std::shared_ptr<Aws::Crt::Http::HttpRequest> &request,
                    const std::shared_ptr<ISigningConfig> &config,
                    const OnHttpRequestSigningComplete &completionCallback) override;

                virtual operator bool() const override
                {
                    return m_signer != nullptr && m_credentialsProvider != nullptr;
                }

              private:
                std::shared_ptr<Sigv4HttpRequestSigner> m_signer;
                std::shared_ptr<ICredentialsProvider> m_credentialsProvider;
            };
        } // namespace Auth
    }     // namespace Crt
} // namespace Aws