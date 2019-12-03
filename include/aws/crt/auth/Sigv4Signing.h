#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Exports.h>

#include <aws/crt/DateTime.h>
#include <aws/crt/Types.h>
#include <aws/crt/auth/Signing.h>

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
                SigV4 = AWS_SIGNING_ALGORITHM_V4,
            };

            enum class SignatureType
            {
                HttpRequestViaHeaders = AWS_ST_HTTP_REQUEST_HEADERS,
                HttpRequestViaQueryParams = AWS_ST_HTTP_REQUEST_QUERY_PARAMS,
                HttpRequestChunk = AWS_ST_HTTP_REQUEST_CHUNK,
                HttpRequestEvent = AWS_ST_HTTP_REQUEST_EVENT,
            };

            enum class SignedBodyValueType
            {
                Empty = AWS_SBVT_EMPTY,
                Payload = AWS_SBVT_PAYLOAD,
                UnsignedPayload = AWS_SBVT_UNSIGNED_PAYLOAD,
                StreamingAws4HmacSha256Payload = AWS_SBVT_STREAMING_AWS4_HMAC_SHA256_PAYLOAD,
                StreamingAws4HmacSha256Events = AWS_SBVT_STREAMING_AWS4_HMAC_SHA256_EVENTS,
            };

            enum class SignedBodyHeaderType
            {
                None = AWS_SBHT_NONE,
                XAmzContentSha256 = AWS_SBHT_X_AMZ_CONTENT_SHA256,
            };

            using ShouldSignHeaderCb = bool (*)(const Crt::ByteCursor *, void *);

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
                 * Gets the type of signature we want to calculate
                 */
                SignatureType GetSignatureType() const noexcept;

                /**
                 * Sets the type of signature we want to calculate
                 */
                void SetSignatureType(SignatureType signatureType) noexcept;

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
                 * Gets whether or not to omit the session token during signing.  Only set to true when performing
                 * a websocket handshake with IoT Core.
                 */
                bool GetOmitSessionToken() const noexcept;

                /**
                 * Sets whether or not to omit the session token during signing.  Only set to true when performing
                 * a websocket handshake with IoT Core.
                 */
                void SetOmitSessionToken(bool omitSessionToken) noexcept;

                /**
                 * Gets the ShouldSignHeadersCb from the underlying config.
                 */
                ShouldSignHeaderCb GetShouldSignHeaderCallback() const noexcept;

                /**
                 * Sets a callback invoked during the signing process for white-listing headers that can be signed.
                 * If you do not set this, all headers will be signed.
                 */
                void SetShouldSignHeaderCallback(ShouldSignHeaderCb shouldSignHeaderCb) noexcept;

                /**
                 * Gets the value to use for the canonical request's payload
                 */
                SignedBodyValueType GetSignedBodyValue() const noexcept;

                /**
                 * Sets the value to use for the canonical request's payload
                 */
                void SetSignedBodyValue(SignedBodyValueType signedBodyValue) noexcept;

                /**
                 * Gets the name of the header to add that stores the signed body value
                 */
                SignedBodyHeaderType GetSignedBodyHeader() const noexcept;

                /**
                 * Sets the name of the header to add that stores the signed body value
                 */
                void SetSignedBodyHeader(SignedBodyHeaderType signedBodyHeader) noexcept;

                /**
                 * (Query param signing only) Gets the amount of time, in seconds, the (pre)signed URI will be good for
                 */
                uint64_t GetExpirationInSeconds() const noexcept;

                /**
                 * (Query param signing only) Sets the amount of time, in seconds, the (pre)signed URI will be good for
                 */
                void SetExpirationInSeconds(uint64_t expirationInSeconds) noexcept;

                /*
                 * For Sigv4 signing, either the credentials provider or the credentials must be set.
                 * Credentials, if set, takes precedence over the provider.
                 */

                /**
                 *  Get the credentials provider to use for signing.
                 */
                const std::shared_ptr<ICredentialsProvider> &GetCredentialsProvider() const noexcept;

                /**
                 *  Set the credentials provider to use for signing.
                 */
                void SetCredentialsProvider(const std::shared_ptr<ICredentialsProvider> &credsProvider) noexcept;

                /**
                 *  Get the credentials to use for signing.
                 */
                const std::shared_ptr<Credentials> &GetCredentials() const noexcept;

                /**
                 *  Set the credentials to use for signing.
                 */
                void SetCredentials(const std::shared_ptr<Credentials> &credentials) noexcept;

                /// @private
                const struct aws_signing_config_aws *GetUnderlyingHandle() const noexcept;

              private:
                Allocator *m_allocator;
                std::shared_ptr<ICredentialsProvider> m_credentialsProvider;
                std::shared_ptr<Credentials> m_credentials;
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
