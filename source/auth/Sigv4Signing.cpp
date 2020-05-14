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

#include <aws/crt/auth/Sigv4Signing.h>

#include <aws/crt/auth/Credentials.h>
#include <aws/crt/http/HttpRequestResponse.h>

#include <aws/auth/signable.h>
#include <aws/auth/signing.h>

namespace Aws
{
    namespace Crt
    {
        namespace Auth
        {
            AwsSigningConfig::AwsSigningConfig(Allocator *allocator)
                : ISigningConfig(), m_allocator(allocator), m_credentialsProvider(nullptr), m_credentials(nullptr)
            {
                AWS_ZERO_STRUCT(m_config);

                SetSigningAlgorithm(SigningAlgorithm::SigV4);
                SetSigningTransform(SigningTransform::Header);
                SetShouldNormalizeUriPath(true);
                SetBodySigningType(BodySigningType::SignBody);
                SetSigningTimepoint(DateTime::Now());
                SetExpirationInSeconds(0);
                m_config.config_type = AWS_SIGNING_CONFIG_AWS;
            }

            AwsSigningConfig::~AwsSigningConfig() { m_allocator = nullptr; }

            SigningAlgorithm AwsSigningConfig::GetSigningAlgorithm() const noexcept
            {
                return static_cast<SigningAlgorithm>(m_config.algorithm);
            }

            void AwsSigningConfig::SetSigningAlgorithm(SigningAlgorithm algorithm) noexcept
            {
                m_config.algorithm = static_cast<aws_signing_algorithm>(algorithm);
            }

            SigningTransform AwsSigningConfig::GetSigningTransform() const noexcept
            {
                return static_cast<SigningTransform>(m_config.transform);
            }

            void AwsSigningConfig::SetSigningTransform(SigningTransform transform) noexcept
            {
                m_config.transform = static_cast<aws_signing_request_transform>(transform);
            }

            const Crt::String &AwsSigningConfig::GetRegion() const noexcept { return m_signingRegion; }

            void AwsSigningConfig::SetRegion(const Crt::String &region) noexcept
            {
                m_signingRegion = region;
                m_config.region = ByteCursorFromCString(m_signingRegion.c_str());
            }

            const Crt::String &AwsSigningConfig::GetService() const noexcept { return m_serviceName; }

            void AwsSigningConfig::SetService(const Crt::String &service) noexcept
            {
                m_serviceName = service;
                m_config.service = ByteCursorFromCString(m_serviceName.c_str());
            }

            DateTime AwsSigningConfig::GetSigningTimepoint() const noexcept
            {
                return {aws_date_time_as_millis(&m_config.date)};
            }

            void AwsSigningConfig::SetSigningTimepoint(const DateTime &date) noexcept
            {
                aws_date_time_init_epoch_millis(&m_config.date, date.Millis());
            }

            bool AwsSigningConfig::GetUseDoubleUriEncode() const noexcept { return m_config.use_double_uri_encode; }

            void AwsSigningConfig::SetUseDoubleUriEncode(bool useDoubleUriEncode) noexcept
            {
                m_config.use_double_uri_encode = useDoubleUriEncode;
            }

            bool AwsSigningConfig::GetShouldNormalizeUriPath() const noexcept
            {
                return m_config.should_normalize_uri_path;
            }

            void AwsSigningConfig::SetShouldNormalizeUriPath(bool shouldNormalizeUriPath) noexcept
            {
                m_config.should_normalize_uri_path = shouldNormalizeUriPath;
            }

            ShouldSignParameterCb AwsSigningConfig::GetShouldSignParameterCallback() const noexcept
            {
                return m_config.should_sign_param;
            }

            void AwsSigningConfig::SetShouldSignHeadersCallback(ShouldSignParameterCb shouldSignParameterCb) noexcept
            {
                m_config.should_sign_param = shouldSignParameterCb;
            }

            BodySigningType AwsSigningConfig::GetBodySigningType() const noexcept
            {
                return static_cast<BodySigningType>(m_config.body_signing_type);
            }

            void AwsSigningConfig::SetBodySigningType(BodySigningType bodysigningType) noexcept
            {
                m_config.body_signing_type = static_cast<enum aws_body_signing_config_type>(bodysigningType);
            }

            uint64_t AwsSigningConfig::GetExpirationInSeconds() const noexcept
            {
                return m_config.expiration_in_seconds;
            }

            void AwsSigningConfig::SetExpirationInSeconds(uint64_t expirationInSeconds) noexcept
            {
                m_config.expiration_in_seconds = expirationInSeconds;
            }

            const std::shared_ptr<ICredentialsProvider> &AwsSigningConfig::GetCredentialsProvider() const noexcept
            {
                return m_credentialsProvider;
            }

            void AwsSigningConfig::SetCredentialsProvider(
                const std::shared_ptr<ICredentialsProvider> &credsProvider) noexcept
            {
                m_credentialsProvider = credsProvider;
                m_config.credentials_provider = m_credentialsProvider->GetUnderlyingHandle();
            }

            const std::shared_ptr<Credentials> &AwsSigningConfig::GetCredentials() const noexcept
            {
                return m_credentials;
            }

            void AwsSigningConfig::SetCredentials(const std::shared_ptr<Credentials> &credentials) noexcept
            {
                m_credentials = credentials;
                m_config.credentials = m_credentials->GetUnderlyingHandle();
            }

            const struct aws_signing_config_aws *AwsSigningConfig::GetUnderlyingHandle() const noexcept
            {
                return &m_config;
            }

            /////////////////////////////////////////////////////////////////////////////////////////////

            Sigv4HttpRequestSigner::Sigv4HttpRequestSigner(Aws::Crt::Allocator *allocator)
                : IHttpRequestSigner(), m_allocator(allocator)
            {
            }

            struct HttpSignerCallbackData
            {
                HttpSignerCallbackData() : Alloc(nullptr) {}
                Allocator *Alloc;
                ScopedResource<struct aws_signable> Signable;
                OnHttpRequestSigningComplete OnRequestSigningComplete;
                std::shared_ptr<Http::HttpRequest> Request;
            };

            static void s_http_signing_complete_fn(struct aws_signing_result *result, int errorCode, void *userdata)
            {
                auto cbData = reinterpret_cast<HttpSignerCallbackData *>(userdata);

                if (errorCode == AWS_OP_SUCCESS)
                {
                    aws_apply_signing_result_to_http_request(
                        cbData->Request->GetUnderlyingMessage(), cbData->Alloc, result);
                }

                cbData->OnRequestSigningComplete(cbData->Request, errorCode);
                Crt::Delete(cbData, cbData->Alloc);
            }

            bool Sigv4HttpRequestSigner::SignRequest(
                const std::shared_ptr<Aws::Crt::Http::HttpRequest> &request,
                const ISigningConfig &config,
                const OnHttpRequestSigningComplete &completionCallback)
            {
                if (config.GetType() != SigningConfigType::Aws)
                {
                    aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
                    return false;
                }

                auto awsSigningConfig = static_cast<const AwsSigningConfig *>(&config);

                if (!awsSigningConfig->GetCredentialsProvider() && !awsSigningConfig->GetCredentials())
                {
                    aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
                    return false;
                }

                auto signerCallbackData = Crt::New<HttpSignerCallbackData>(m_allocator);

                if (!signerCallbackData)
                {
                    return false;
                }

                signerCallbackData->Alloc = m_allocator;
                signerCallbackData->OnRequestSigningComplete = completionCallback;
                signerCallbackData->Request = request;
                signerCallbackData->Signable = ScopedResource<struct aws_signable>(
                    aws_signable_new_http_request(m_allocator, request->GetUnderlyingMessage()), aws_signable_destroy);

                return aws_sign_request_aws(
                           m_allocator,
                           signerCallbackData->Signable.get(),
                           (aws_signing_config_base *)awsSigningConfig->GetUnderlyingHandle(),
                           s_http_signing_complete_fn,
                           signerCallbackData) == AWS_OP_SUCCESS;
            }
        } // namespace Auth
    }     // namespace Crt
} // namespace Aws
