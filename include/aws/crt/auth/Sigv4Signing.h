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

            enum class BodySigningType
            {
                NoSigning = AWS_BODY_SIGNING_OFF,
                SignBody = AWS_BODY_SIGNING_ON,
                UnsignedPayload = AWS_BODY_SIGNING_UNSIGNED_PAYLOAD
            };

            using ShouldSignParameterCb = bool (*)(const Crt::ByteCursor *, void *);

            /**
             * Wrapper around the configuration structure specific to the AWS
             * Sigv4 signing process
             */
            class AWS_CRT_CPP_API AwsSigningConfig : public ISigningConfig
            {
              public:
                AwsSigningConfig(Allocator *allocator = g_allocator);
                virtual ~AwsSigningConfig();

                virtual SigningConfigType GetType() const noexcept override { return SigningConfigType::Aws; }

                /**
                 * Gets the signing process we want to invoke
                 */
                SigningAlgorithm GetSigningAlgorithm() const noexcept;

                /**
                 * Sets the signing process we want to invoke
                 */
                void SetSigningAlgorithm(SigningAlgorithm algorithm) noexcept;

                /**
                 * Gets the AWS region to sign against
                 */
                const Crt::String &GetRegion() const noexcept;

                /**
                 * Sets the AWS region to sign against
                 */
                void SetRegion(const Crt::String &region) noexcept;

                /**
                 * Gets the (signing) name of the AWS service to sign a request for
                 */
                const Crt::String &GetService() const noexcept;

                /**
                 * Sets the (signing) name of the AWS service to sign a request for
                 */
                void SetService(const Crt::String &service) noexcept;

                /**
                 * Gets the timestamp to use during the signing process.
                 */
                DateTime GetSigningTimepoint() const noexcept;

                /**
                 * Sets the timestamp to use during the signing process.
                 */
                void SetSigningTimepoint(const DateTime &date) noexcept;

                /*
                 * We assume the uri will be encoded once in preparation for transmission.  Certain services
                 * do not decode before checking signature, requiring us to actually double-encode the uri in the
                 * canonical request in order to pass a signature check.
                 */

                /**
                 * Gets whether or not the signing process should perform a uri encode step before creating the
                 * canonical request.
                 */
                bool GetUseDoubleUriEncode() const noexcept;

                /**
                 * Sets whether or not the signing process should perform a uri encode step before creating the
                 * canonical request.
                 */
                void SetUseDoubleUriEncode(bool useDoubleUriEncode) noexcept;

                /**
                 * Gets whether or not the uri paths should be normalized when building the canonical request
                 */
                bool GetShouldNormalizeUriPath() const noexcept;

                /**
                 * Sets whether or not the uri paths should be normalized when building the canonical request
                 */
                void SetShouldNormalizeUriPath(bool shouldNormalizeUriPath) noexcept;

                /**
                 * Gets the ShouldSignHeadersCb from the underlying config.
                 */
                ShouldSignParameterCb GetShouldSignParameterCallback() const noexcept;

                /**
                 * Sets a callback invoked during the signing process for white-listing headers that can be signed.
                 * If you do not set this, all headers will be signed.
                 */
                void SetShouldSignHeadersCallback(ShouldSignParameterCb shouldSignParameterCb) noexcept;

                /**
                 * Gets whether or not the signer should add the x-amz-content-sha256 header (with appropriate value) to
                 * the canonical request.
                 */
                BodySigningType GetBodySigningType() const noexcept;

                /**
                 * Sets whether or not the signer should add the x-amz-content-sha256 header (with appropriate value) to
                 * the canonical request.
                 */
                void SetBodySigningType(BodySigningType bodysigningType) noexcept;

                /**
                 * Gets whether or not the signer should use 'UNSIGNED_PAYLOAD' for the payload hash. This is only ever
                 * used for S3.
                 */
                bool GetUseUnsignedPayloadHash() const noexcept;

                /**
                 * Sets whether or not the signer should use 'UNSIGNED_PAYLOAD' for the payload hash. This is only ever
                 * used for S3.
                 */
                void SetUseUnsignedPayloadHash(bool useUnsignedPayload) noexcept;

                /**
                 *  Get the credentials provider to use for signing.
                 */
                const std::shared_ptr<ICredentialsProvider> &GetCredentialsProvider() const noexcept;

                /**
                 *  Set the credentials provider to use for signing, this is mandatory for sigv4.
                 */
                void SetCredentialsProvider(const std::shared_ptr<ICredentialsProvider> &credsProvider) noexcept;

                const struct aws_signing_config_aws *GetUnderlyingHandle() const noexcept;

              private:
                Allocator *m_allocator;
                std::shared_ptr<ICredentialsProvider> m_credentials;
                struct aws_signing_config_aws m_config;
                Crt::String m_signingRegion;
                Crt::String m_serviceName;
            };

            /**
             * Http request signer that performs Aws Sigv4 signing
             */
            class AWS_CRT_CPP_API Sigv4HttpRequestSigner : public IHttpRequestSigner
            {
              public:
                Sigv4HttpRequestSigner(Allocator *allocator = g_allocator);
                virtual ~Sigv4HttpRequestSigner() = default;

                bool IsValid() const override { return true; }
                /**
                 * Signs an http request with AWS-auth sigv4. OnCompletionCallback will be invoked upon completion.
                 */
                virtual bool SignRequest(
                    const std::shared_ptr<Aws::Crt::Http::HttpRequest> &request,
                    const ISigningConfig &config,
                    const OnHttpRequestSigningComplete &completionCallback) override;

              private:
                Allocator *m_allocator;
            };
        } // namespace Auth
    }     // namespace Crt
} // namespace Aws
