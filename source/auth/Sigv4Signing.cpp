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
#include <aws/auth/signer.h>

namespace Aws
{
    namespace Crt
    {
        namespace Auth
        {
            AwsSigningConfig::AwsSigningConfig(Allocator *allocator)
                : ISigningConfig(), m_allocator(allocator), m_credentials(nullptr),
                  m_config(Aws::Crt::New<aws_signing_config_aws>(allocator))
            {
                AWS_ZERO_STRUCT(*m_config);

                SetSigningAlgorithm(SigningAlgorithm::SigV4Header);
                SetShouldNormalizeUriPath(true);
                SetSignBody(true);
            }

            AwsSigningConfig::~AwsSigningConfig()
            {
                if (m_config != nullptr)
                {
                    Aws::Crt::Delete(m_config, m_allocator);
                    m_config = nullptr;
                }

                m_allocator = nullptr;
            }

            std::shared_ptr<Credentials> AwsSigningConfig::GetCredentials() const noexcept { return m_credentials; }

            void AwsSigningConfig::SetCredentials(const std::shared_ptr<Credentials> &credentials) noexcept
            {
                m_config->credentials = credentials->GetUnderlyingHandle();
                m_credentials = credentials;
            }

            SigningAlgorithm AwsSigningConfig::GetSigningAlgorithm() const noexcept
            {
                return static_cast<SigningAlgorithm>(m_config->algorithm);
            }

            void AwsSigningConfig::SetSigningAlgorithm(SigningAlgorithm algorithm) noexcept
            {
                m_config->algorithm = static_cast<aws_signing_algorithm>(algorithm);
            }

            ByteCursor AwsSigningConfig::GetRegion() const noexcept { return m_config->region; }

            void AwsSigningConfig::SetRegion(ByteCursor region) noexcept { m_config->region = region; }

            ByteCursor AwsSigningConfig::GetService() const noexcept { return m_config->service; }

            void AwsSigningConfig::SetService(ByteCursor service) noexcept { m_config->service = service; }

            DateTime AwsSigningConfig::GetDate() const noexcept
            {
                return DateTime(aws_date_time_as_millis(&m_config->date));
            }

            void AwsSigningConfig::SetDate(const DateTime &date) noexcept
            {
                aws_date_time_init_epoch_millis(&m_config->date, date.Millis());
            }

            bool AwsSigningConfig::GetUseDoubleUriEncode() const noexcept { return m_config->use_double_uri_encode; }

            void AwsSigningConfig::SetUseDoubleUriEncode(bool useDoubleUriEncode) noexcept
            {
                m_config->use_double_uri_encode = useDoubleUriEncode;
            }

            bool AwsSigningConfig::GetShouldNormalizeUriPath() const noexcept
            {
                return m_config->should_normalize_uri_path;
            }

            void AwsSigningConfig::SetShouldNormalizeUriPath(bool shouldNormalizeUriPath) noexcept
            {
                m_config->should_normalize_uri_path = shouldNormalizeUriPath;
            }

            bool AwsSigningConfig::GetSignBody() const noexcept { return m_config->sign_body; }

            void AwsSigningConfig::SetSignBody(bool signBody) noexcept { m_config->sign_body = signBody; }

            /////////////////////////////////////////////////////////////////////////////////////////////

            AwsCHttpRequestSigner::AwsCHttpRequestSigner(struct aws_signer *signer, Allocator *allocator)
                : IHttpRequestSigner(), m_allocator(allocator), m_signer(signer)
            {
            }

            AwsCHttpRequestSigner::~AwsCHttpRequestSigner()
            {
                if (m_signer != nullptr)
                {
                    aws_signer_destroy(m_signer);
                }
                m_signer = nullptr;
                m_allocator = nullptr;
            }

            /////////////////////////////////////////////////////////////////////////////////////////////

            Sigv4HttpRequestSigner::Sigv4HttpRequestSigner(Aws::Crt::Allocator *allocator)
                : AwsCHttpRequestSigner(aws_signer_new_aws(allocator), allocator)
            {
            }

            Sigv4HttpRequestSigningPipeline::~Sigv4HttpRequestSigningPipeline()
            {
                m_signer = nullptr;
                m_credentialsProvider = nullptr;
            }

            bool Sigv4HttpRequestSigner::SignRequest(Aws::Crt::Http::HttpRequest &request, const ISigningConfig *config)
            {
                if (config->GetType() != SigningConfigType::Aws)
                {
                    aws_raise_error(AWS_ERROR_INVALID_ARGUMENT);
                    return false;
                }

                const AwsSigningConfig *awsSigningConfig = static_cast<const AwsSigningConfig *>(config);

                ScopedResource<aws_signable> scoped_signable(
                    aws_signable_new_http_request(m_allocator, request.GetUnderlyingMessage()), aws_signable_destroy);
                if (scoped_signable.GetResource() == nullptr)
                {
                    return false;
                }

                struct aws_signing_config_aws signingConfig;
                AWS_ZERO_STRUCT(signingConfig);
                signingConfig.config_type = AWS_SIGNING_CONFIG_AWS;
                signingConfig.algorithm = (enum aws_signing_algorithm)awsSigningConfig->GetSigningAlgorithm();
                signingConfig.credentials = awsSigningConfig->GetCredentials()->GetUnderlyingHandle();
                signingConfig.region = awsSigningConfig->GetRegion();
                signingConfig.service = awsSigningConfig->GetService();
                signingConfig.use_double_uri_encode = awsSigningConfig->GetUseDoubleUriEncode();
                signingConfig.should_normalize_uri_path = awsSigningConfig->GetShouldNormalizeUriPath();
                signingConfig.sign_body = awsSigningConfig->GetSignBody();

                aws_date_time_init_epoch_millis(&signingConfig.date, awsSigningConfig->GetDate().Millis());

                struct aws_signing_result signing_result;
                AWS_ZERO_STRUCT(signing_result);
                aws_signing_result_init(&signing_result, m_allocator);

                ScopedResource<aws_signing_result> scoped_signing_result(&signing_result, aws_signing_result_clean_up);

                if (aws_signer_sign_request(
                        m_signer,
                        scoped_signable.GetResource(),
                        (aws_signing_config_base *)&signingConfig,
                        &signing_result) != AWS_OP_SUCCESS)
                {
                    return false;
                }

                if (aws_apply_signing_result_to_http_request(
                        request.GetUnderlyingMessage(), m_allocator, &signing_result) != AWS_OP_SUCCESS)
                {
                    return false;
                }

                return true;
            }

            /////////////////////////////////////////////////////////////////////////////////////////////

            Sigv4HttpRequestSigningPipeline::Sigv4HttpRequestSigningPipeline(
                const std::shared_ptr<ICredentialsProvider> &credentialsProvider,
                Allocator *allocator)
                : m_signer(Aws::Crt::MakeShared<Sigv4HttpRequestSigner>(allocator, allocator)),
                  m_credentialsProvider(credentialsProvider)
            {
            }

            void Sigv4HttpRequestSigningPipeline::SignRequest(
                const std::shared_ptr<Aws::Crt::Http::HttpRequest> &request,
                const std::shared_ptr<ISigningConfig> &config,
                const OnSigningComplete &completionCallback)
            {
                if (config->GetType() != SigningConfigType::Aws)
                {
                    completionCallback(request, AWS_ERROR_INVALID_ARGUMENT);
                    return;
                }

                auto getCredentialsCallback = [&, this](std::shared_ptr<Credentials> credentials) {
                    if (credentials == nullptr)
                    {
                        completionCallback(request, AWS_AUTH_SIGNING_NO_CREDENTIALS);
                    }
                    else
                    {
                        int error = AWS_ERROR_SUCCESS;
                        AwsSigningConfig *awsSigningConfig = static_cast<AwsSigningConfig *>(config.get());
                        awsSigningConfig->SetCredentials(credentials);
                        if (!m_signer->SignRequest(*request, config.get()))
                        {
                            error = aws_last_error();
                            if (error == AWS_ERROR_SUCCESS)
                            {
                                error = AWS_ERROR_UNKNOWN;
                            }
                        }
                        completionCallback(request, error);
                    }
                };

                if (!m_credentialsProvider->GetCredentials(getCredentialsCallback))
                {
                    completionCallback(request, AWS_ERROR_UNKNOWN);
                }
            }
        } // namespace Auth
    }     // namespace Crt
} // namespace Aws
