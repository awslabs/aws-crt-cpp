/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/s3/S3.h>

#include <aws/crt/Api.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/io/Bootstrap.h>
#include <aws/crt/io/EventLoopGroup.h>

#include <aws/auth/signing_config.h>
#include <aws/http/connection.h>
#include <aws/http/proxy.h>
#include <aws/http/request_response.h>
#include <aws/io/retry_strategy.h>
#include <aws/io/uri.h>
#include <aws/s3/s3_client.h>

namespace Aws
{
    namespace Crt
    {
        namespace S3
        {
            S3ChecksumConfig::S3ChecksumConfig() noexcept
                // Trailer + CRC64NVME matches the AWS SDK default. A location
                // of None disables the algorithm entirely.
                : m_location(S3ChecksumLocation::Trailer), m_algorithm(S3ChecksumAlgorithm::Crc64Nvme),
                  m_validateResponseChecksum(false)
            {
            }

            S3RetryStrategy::S3RetryStrategy(aws_retry_strategy *strategy) noexcept
                : m_strategy(strategy, aws_retry_strategy_release)
            {
            }

            S3RetryStrategy S3RetryStrategy::CreateStandard() noexcept
            {
                struct aws_standard_retry_options options;
                AWS_ZERO_STRUCT(options);
                return S3RetryStrategy(aws_retry_strategy_new_standard(ApiAllocator(), &options));
            }

            S3RetryStrategy S3RetryStrategy::CreateExponentialBackoff(
                Io::EventLoopGroup &elGroup,
                const S3RetryStrategyExponentialBackoffOptions &options) noexcept
            {
                struct aws_exponential_backoff_retry_options backoffOptions;
                AWS_ZERO_STRUCT(backoffOptions);
                backoffOptions.el_group = elGroup.GetUnderlyingHandle();
                // maxRetries == 0 is passed through as-is; aws-c-io reads 0 as
                // "unset" and substitutes its default (5). Callers wanting zero
                // retries should use S3RetryStrategyType::NoRetry instead.
                backoffOptions.max_retries = options.maxRetries;
                backoffOptions.backoff_scale_factor_ms = options.scaleFactorMs;
                backoffOptions.max_backoff_secs = options.maxBackoffSecs;
                backoffOptions.jitter_mode = AWS_EXPONENTIAL_BACKOFF_JITTER_FULL;
                return S3RetryStrategy(aws_retry_strategy_new_exponential_backoff(ApiAllocator(), &backoffOptions));
            }

            S3RetryStrategy S3RetryStrategy::CreateNoRetry() noexcept
            {
                struct aws_no_retry_options options;
                AWS_ZERO_STRUCT(options);
                return S3RetryStrategy(aws_retry_strategy_new_no_retry(ApiAllocator(), &options));
            }

            // Holds all CRT C-struct storage for S3ClientConfig by value, so the
            // whole set is one heap allocation (the Impl) instead of one per
            // struct. Value-initialized ({}) so every C struct starts zeroed,
            // matching the previous aws_mem_calloc behavior.
            struct S3ClientConfig::Impl
            {
                aws_s3_client_config config = {};
                aws_http_proxy_options proxyOptions = {};
                aws_s3_tcp_keep_alive_options tcpKeepAlive = {};
                aws_http_connection_monitoring_options monitoring = {};
            };

            S3ClientConfig::S3ClientConfig(
                const std::shared_ptr<Auth::ICredentialsProvider> &credentialsProvider) noexcept
                : m_impl(New<Impl>(ApiAllocator()), [](Impl *p) { Delete(p, ApiAllocator()); }), m_region("us-east-1"),
                  m_retryStrategyType(S3RetryStrategyType::Default), m_credentialsProvider(credentialsProvider)
            {
                m_impl->config.region = ByteCursorFromString(m_region);

                // Build the signing config in place (no heap). Left empty when
                // no credentials provider is supplied; a null signing_config
                // makes aws_s3_client_new fail with AWS_ERROR_INVALID_ARGUMENT,
                // surfaced via LastError().
                if (credentialsProvider)
                {
                    m_signingConfig.emplace(ApiAllocator());
                    if (S3Client::MakeDefaultSigningConfig(*m_signingConfig, m_region, credentialsProvider))
                    {
                        m_impl->config.signing_config = m_signingConfig->GetUnderlyingHandle();
                    }
                    else
                    {
                        m_signingConfig.reset();
                    }
                }

                // S3 Express on by default to match the AWS SDK S3 client.
                m_impl->config.enable_s3express = true;
            }

            struct aws_s3_client_config *S3ClientConfig::GetUnderlyingHandle() const noexcept
            {
                return &m_impl->config;
            }

            // Out-of-line so the ScopedResource<Impl> is destroyed where Impl is
            // a complete type.
            S3ClientConfig::~S3ClientConfig() noexcept = default;

            S3ClientConfig &S3ClientConfig::SetRegion(const Crt::String &region) noexcept
            {
                m_region = region;
                m_impl->config.region = ByteCursorFromString(m_region);
                // Keep the signing config's region in sync with the client's.
                if (m_signingConfig)
                {
                    m_signingConfig->SetRegion(m_region);
                }
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetThroughputTargetGbps(double gbps) noexcept
            {
                m_impl->config.throughput_target_gbps = gbps;
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetPartSize(uint64_t bytes) noexcept
            {
                m_impl->config.part_size = bytes;
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetMultipartUploadThreshold(uint64_t bytes) noexcept
            {
                m_impl->config.multipart_upload_threshold = bytes;
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetMemoryLimit(uint64_t bytes) noexcept
            {
                m_impl->config.memory_limit_in_bytes = bytes;
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetClientBootstrap(Io::ClientBootstrap &bootstrap) noexcept
            {
                m_impl->config.client_bootstrap = bootstrap.GetUnderlyingHandle();
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetConnectTimeoutMs(uint32_t timeoutMs) noexcept
            {
                m_impl->config.connect_timeout_ms = timeoutMs;
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetReadBackpressure(bool enable, uint64_t initialReadWindow) noexcept
            {
                m_impl->config.enable_read_backpressure = enable;
                m_impl->config.initial_read_window = static_cast<size_t>(initialReadWindow);
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetEnableS3Express(bool enable) noexcept
            {
                m_impl->config.enable_s3express = enable;
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetTlsMode(S3TlsMode mode) noexcept
            {
                switch (mode)
                {
                    case S3TlsMode::Enabled:
                        m_impl->config.tls_mode = AWS_MR_TLS_ENABLED;
                        break;
                    case S3TlsMode::Disabled:
                        m_impl->config.tls_mode = AWS_MR_TLS_DISABLED;
                        break;
                }
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetTlsConnectionOptions(const Io::TlsConnectionOptions &options) noexcept
            {
                m_tlsConnectionOptions = options;
                m_impl->config.tls_connection_options = m_tlsConnectionOptions->GetUnderlyingHandle();
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetProxyOptions(
                const Http::HttpClientConnectionProxyOptions &proxyOptions) noexcept
            {
                m_proxyOptions = proxyOptions;
                m_proxyOptions->InitializeRawProxyOptions(m_impl->proxyOptions);
                m_impl->config.proxy_options = &m_impl->proxyOptions;
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetTcpKeepAlive(
                uint16_t keepAliveIntervalSec,
                uint16_t keepAliveTimeoutSec) noexcept
            {
                m_impl->tcpKeepAlive.keep_alive_interval_sec = keepAliveIntervalSec;
                m_impl->tcpKeepAlive.keep_alive_timeout_sec = keepAliveTimeoutSec;
                m_impl->config.tcp_keep_alive_options = &m_impl->tcpKeepAlive;
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetConnectionMonitoring(
                uint64_t minimumThroughputBytesPerSecond,
                uint32_t allowableThroughputFailureIntervalSeconds) noexcept
            {
                m_impl->monitoring.minimum_throughput_bytes_per_second = minimumThroughputBytesPerSecond;
                m_impl->monitoring.allowable_throughput_failure_interval_seconds =
                    allowableThroughputFailureIntervalSeconds;
                m_impl->config.monitoring_options = &m_impl->monitoring;
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetNetworkInterfaceNames(
                const Vector<String> &networkInterfaceNames) noexcept
            {
                // Cursor array is built as a local at S3Client construction
                // (deep-copied there); only the backing strings live here.
                m_networkInterfaceNames = networkInterfaceNames;
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetRetryStrategy(
                S3RetryStrategyType type,
                const S3RetryStrategyExponentialBackoffOptions &options) noexcept
            {
                // Flavor and factory paths are mutually exclusive; the
                // strategy is materialized at S3Client construction.
                m_retryStrategyType = type;
                m_retryStrategyOptions = options;
                m_retryStrategyFactory = nullptr;
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetRetryStrategyFactory(
                std::function<S3RetryStrategy(const S3ClientConfig &)> factory) noexcept
            {
                m_retryStrategyFactory = std::move(factory);
                return *this;
            }

            S3ClientConfig &S3ClientConfig::SetClientShutdownCallback(std::function<void()> callback) noexcept
            {
                m_clientShutdownCallback = std::move(callback);
                return *this;
            }

            // Holds the CRT C-struct storage for S3MetaRequestOptions by value, so
            // the options struct and its checksum-config backing are one heap
            // allocation (the Impl) instead of two. Value-initialized ({}) so both
            // start zeroed, matching the previous aws_mem_calloc behavior.
            struct S3MetaRequestOptions::Impl
            {
                aws_s3_meta_request_options options = {};
                aws_s3_checksum_config checksum = {};
            };

            S3MetaRequestOptions::S3MetaRequestOptions(
                S3MetaRequestType type,
                const std::shared_ptr<Http::HttpRequest> &request) noexcept
                : m_impl(New<Impl>(ApiAllocator()), [](Impl *p) { Delete(p, ApiAllocator()); }), m_httpRequest(request),
                  m_endpointInit(false)
            {
                AWS_ZERO_STRUCT(m_endpoint);
                switch (type)
                {
                    case S3MetaRequestType::Default:
                        m_impl->options.type = AWS_S3_META_REQUEST_TYPE_DEFAULT;
                        break;
                    case S3MetaRequestType::GetObject:
                        m_impl->options.type = AWS_S3_META_REQUEST_TYPE_GET_OBJECT;
                        break;
                    case S3MetaRequestType::PutObject:
                        m_impl->options.type = AWS_S3_META_REQUEST_TYPE_PUT_OBJECT;
                        break;
                    case S3MetaRequestType::CopyObject:
                        m_impl->options.type = AWS_S3_META_REQUEST_TYPE_COPY_OBJECT;
                        break;
                }
                m_impl->options.message = request ? request->GetUnderlyingMessage() : nullptr;
            }

            struct aws_s3_meta_request_options *S3MetaRequestOptions::GetUnderlyingHandle() const noexcept
            {
                return &m_impl->options;
            }

            S3MetaRequestOptions::~S3MetaRequestOptions() noexcept
            {
                if (m_endpointInit)
                {
                    aws_uri_clean_up(&m_endpoint);
                }
            }

            S3MetaRequestOptions &S3MetaRequestOptions::SetSigningConfig(const Auth::AwsSigningConfig &config) noexcept
            {
                m_impl->options.signing_config = config.GetUnderlyingHandle();
                return *this;
            }

            S3MetaRequestOptions &S3MetaRequestOptions::SetChecksumConfig(const S3ChecksumConfig &config) noexcept
            {
                switch (config.GetLocation())
                {
                    case S3ChecksumLocation::None:
                        m_impl->checksum.location = AWS_SCL_NONE;
                        break;
                    case S3ChecksumLocation::Header:
                        m_impl->checksum.location = AWS_SCL_HEADER;
                        break;
                    case S3ChecksumLocation::Trailer:
                        m_impl->checksum.location = AWS_SCL_TRAILER;
                        break;
                }
                switch (config.GetChecksumAlgorithm())
                {
                    case S3ChecksumAlgorithm::None:
                        m_impl->checksum.checksum_algorithm = AWS_SCA_NONE;
                        break;
                    case S3ChecksumAlgorithm::Crc32c:
                        m_impl->checksum.checksum_algorithm = AWS_SCA_CRC32C;
                        break;
                    case S3ChecksumAlgorithm::Crc32:
                        m_impl->checksum.checksum_algorithm = AWS_SCA_CRC32;
                        break;
                    case S3ChecksumAlgorithm::Sha1:
                        m_impl->checksum.checksum_algorithm = AWS_SCA_SHA1;
                        break;
                    case S3ChecksumAlgorithm::Sha256:
                        m_impl->checksum.checksum_algorithm = AWS_SCA_SHA256;
                        break;
                    case S3ChecksumAlgorithm::Crc64Nvme:
                        m_impl->checksum.checksum_algorithm = AWS_SCA_CRC64NVME;
                        break;
                    case S3ChecksumAlgorithm::Sha512:
                        m_impl->checksum.checksum_algorithm = AWS_SCA_SHA512;
                        break;
                    case S3ChecksumAlgorithm::XXHash64:
                        m_impl->checksum.checksum_algorithm = AWS_SCA_XXHASH64;
                        break;
                    case S3ChecksumAlgorithm::XXHash3_64:
                        m_impl->checksum.checksum_algorithm = AWS_SCA_XXHASH3_64;
                        break;
                    case S3ChecksumAlgorithm::XXHash3_128:
                        m_impl->checksum.checksum_algorithm = AWS_SCA_XXHASH3_128;
                        break;
                }
                m_impl->checksum.validate_response_checksum = config.GetValidateResponseChecksum();
                m_impl->options.checksum_config = &m_impl->checksum;
                return *this;
            }

            S3MetaRequestOptions &S3MetaRequestOptions::SetEndpoint(const Io::Uri &endpoint) noexcept
            {
                // Clear any previously-parsed endpoint before replacing it.
                if (m_endpointInit)
                {
                    aws_uri_clean_up(&m_endpoint);
                    AWS_ZERO_STRUCT(m_endpoint);
                    m_endpointInit = false;
                }
                m_impl->options.endpoint = nullptr;

                // A malformed endpoint is a hard error, never silently dropped.
                // The failure is sticky and surfaced by MakeMetaRequest, since
                // this fluent setter has no error return (mirrors the MqttClient
                // builder's LastError() pattern).
                if (!endpoint)
                {
                    m_lastError = endpoint.LastError();
                    return *this;
                }

                // Parse into the value member in place: aws_uri's cursors point
                // into its own buffer, so it must not be moved after parsing. The
                // CRT reads host/scheme/port synchronously during MakeMetaRequest,
                // so this only needs to outlive that call.
                ByteCursor fullUri = endpoint.GetFullUri();
                if (aws_uri_init_parse(&m_endpoint, ApiAllocator(), &fullUri) != AWS_OP_SUCCESS)
                {
                    m_lastError = aws_last_error();
                    AWS_ZERO_STRUCT(m_endpoint);
                    return *this;
                }
                m_endpointInit = true;
                m_impl->options.endpoint = &m_endpoint;
                return *this;
            }

            S3MetaRequestOptions &S3MetaRequestOptions::SetHeadersCallback(HeadersCallback cb) noexcept
            {
                m_headersCb = std::move(cb);
                return *this;
            }
            S3MetaRequestOptions &S3MetaRequestOptions::SetProgressCallback(ProgressCallback cb) noexcept
            {
                m_progressCb = std::move(cb);
                return *this;
            }
            S3MetaRequestOptions &S3MetaRequestOptions::SetFinishCallback(FinishCallback cb) noexcept
            {
                m_finishCb = std::move(cb);
                return *this;
            }
            S3MetaRequestOptions &S3MetaRequestOptions::SetShutdownCallback(ShutdownCallback cb) noexcept
            {
                m_shutdownCb = std::move(cb);
                return *this;
            }

            S3MetaRequestOptions &S3MetaRequestOptions::SetRecvFileMode(S3RecvFileMode mode) noexcept
            {
                switch (mode)
                {
                    case S3RecvFileMode::CreateOrReplace:
                        m_impl->options.recv_file_option = AWS_S3_RECV_FILE_CREATE_OR_REPLACE;
                        break;
                    case S3RecvFileMode::CreateNew:
                        m_impl->options.recv_file_option = AWS_S3_RECV_FILE_CREATE_NEW;
                        break;
                    case S3RecvFileMode::CreateOrAppend:
                        m_impl->options.recv_file_option = AWS_S3_RECV_FILE_CREATE_OR_APPEND;
                        break;
                    case S3RecvFileMode::WriteToPosition:
                        m_impl->options.recv_file_option = AWS_S3_RECV_FILE_WRITE_TO_POSITION;
                        break;
                }
                return *this;
            }

            S3MetaRequestOptions &S3MetaRequestOptions::SetRecvFilePosition(uint64_t position) noexcept
            {
                m_impl->options.recv_file_position = position;
                return *this;
            }

            S3MetaRequestOptions &S3MetaRequestOptions::SetRecvFileDeleteOnFailure(bool deleteOnFailure) noexcept
            {
                m_impl->options.recv_file_delete_on_failure = deleteOnFailure;
                return *this;
            }

            S3MetaRequestOptions &S3MetaRequestOptions::SetObjectSizeHint(uint64_t bytes) noexcept
            {
                // CRT borrows const uint64_t*, so the value must live here;
                // bytes == 0 clears the hint and releases the borrowed pointer.
                if (bytes == 0)
                {
                    m_objectSizeHint.reset();
                    m_impl->options.object_size_hint = nullptr;
                }
                else
                {
                    m_objectSizeHint = bytes;
                    m_impl->options.object_size_hint = &m_objectSizeHint.value();
                }
                return *this;
            }

            S3MetaRequestOptions &S3MetaRequestOptions::SetPartSize(uint64_t bytes) noexcept
            {
                m_impl->options.part_size = bytes;
                return *this;
            }

            S3MetaRequestOptions &S3MetaRequestOptions::SetMultipartUploadThreshold(uint64_t bytes) noexcept
            {
                m_impl->options.multipart_upload_threshold = bytes;
                return *this;
            }

            /*****************************************************
             *
             * S3GetObjectMetaRequestOptions
             *
             *****************************************************/
            // Allocate the subclass through the CRT allocator and wrap it in a
            // unique_ptr whose deleter frees through that same allocator. The
            // ScopedResource<Base> upcasts from the subclass pointer while
            // retaining the correct destructor via its deleter.
            template <typename SubclassT, typename... Args>
            static ScopedResource<S3MetaRequestOptions> s_makeOptions(Args &&...args) noexcept
            {
                Allocator *allocator = ApiAllocator();
                return ScopedResource<S3MetaRequestOptions>(
                    New<SubclassT>(allocator, std::forward<Args>(args)...),
                    [allocator](S3MetaRequestOptions *p)
                    {
                        if (p != nullptr)
                        {
                            Aws::Crt::Delete(p, allocator);
                        }
                    });
            }

            // Build a SubclassT options object from request, then run configure()
            // on the concrete subclass pointer to set its shape-specific fields.
            // Folds the make + null-check + downcast that every configuring Create
            // factory would otherwise repeat.
            template <typename SubclassT, typename ConfigureFn>
            static ScopedResource<S3MetaRequestOptions> s_makeConfiguredOptions(
                const std::shared_ptr<Http::HttpRequest> &request,
                ConfigureFn &&configure) noexcept
            {
                auto opts = s_makeOptions<SubclassT>(request);
                if (opts)
                {
                    configure(static_cast<SubclassT *>(opts.get()));
                }
                return opts;
            }

            S3GetObjectMetaRequestOptions::S3GetObjectMetaRequestOptions(
                const std::shared_ptr<Http::HttpRequest> &request) noexcept
                : S3MetaRequestOptions(S3MetaRequestType::GetObject, request)
            {
            }

            ScopedResource<S3MetaRequestOptions> S3GetObjectMetaRequestOptions::Create(
                const std::shared_ptr<Http::HttpRequest> &request,
                BodyCallback cb) noexcept
            {
                return s_makeConfiguredOptions<S3GetObjectMetaRequestOptions>(
                    request, [&](S3GetObjectMetaRequestOptions *self) { self->m_bodyCb = std::move(cb); });
            }

            ScopedResource<S3MetaRequestOptions> S3GetObjectMetaRequestOptions::Create(
                const std::shared_ptr<Http::HttpRequest> &request,
                BodyCallbackEx cb) noexcept
            {
                return s_makeConfiguredOptions<S3GetObjectMetaRequestOptions>(
                    request, [&](S3GetObjectMetaRequestOptions *self) { self->m_bodyCbEx = std::move(cb); });
            }

            ScopedResource<S3MetaRequestOptions> S3GetObjectMetaRequestOptions::Create(
                const std::shared_ptr<Http::HttpRequest> &request,
                const Crt::String &recvFilepath) noexcept
            {
                return s_makeConfiguredOptions<S3GetObjectMetaRequestOptions>(
                    request,
                    [&](S3GetObjectMetaRequestOptions *self)
                    {
                        self->m_recvFilepath = recvFilepath;
                        self->m_impl->options.recv_filepath = ByteCursorFromString(self->m_recvFilepath);
                    });
            }

            /*****************************************************
             *
             * S3PutObjectMetaRequestOptions
             *
             *****************************************************/
            S3PutObjectMetaRequestOptions::S3PutObjectMetaRequestOptions(
                const std::shared_ptr<Http::HttpRequest> &request) noexcept
                : S3MetaRequestOptions(S3MetaRequestType::PutObject, request)
            {
            }

            ScopedResource<S3MetaRequestOptions> S3PutObjectMetaRequestOptions::Create(
                const std::shared_ptr<Http::HttpRequest> &request) noexcept
            {
                return s_makeOptions<S3PutObjectMetaRequestOptions>(request);
            }

            ScopedResource<S3MetaRequestOptions> S3PutObjectMetaRequestOptions::Create(
                const std::shared_ptr<Http::HttpRequest> &request,
                const Crt::String &sendFilepath) noexcept
            {
                return s_makeConfiguredOptions<S3PutObjectMetaRequestOptions>(
                    request,
                    [&](S3PutObjectMetaRequestOptions *self)
                    {
                        self->m_sendFilepath = sendFilepath;
                        self->m_impl->options.send_filepath = ByteCursorFromString(self->m_sendFilepath);
                    });
            }

            ScopedResource<S3MetaRequestOptions> S3PutObjectMetaRequestOptions::CreateWithAsyncWrites(
                const std::shared_ptr<Http::HttpRequest> &request) noexcept
            {
                return s_makeConfiguredOptions<S3PutObjectMetaRequestOptions>(
                    request,
                    [](S3PutObjectMetaRequestOptions *self) { self->m_impl->options.send_using_async_writes = true; });
            }

            /*****************************************************
             *
             * S3CopyObjectMetaRequestOptions
             *
             *****************************************************/
            S3CopyObjectMetaRequestOptions::S3CopyObjectMetaRequestOptions(
                const std::shared_ptr<Http::HttpRequest> &request) noexcept
                : S3MetaRequestOptions(S3MetaRequestType::CopyObject, request)
            {
            }

            ScopedResource<S3MetaRequestOptions> S3CopyObjectMetaRequestOptions::Create(
                const std::shared_ptr<Http::HttpRequest> &request) noexcept
            {
                return s_makeOptions<S3CopyObjectMetaRequestOptions>(request);
            }

            /*****************************************************
             *
             * S3DefaultObjectMetaRequestOptions
             *
             *****************************************************/
            S3DefaultObjectMetaRequestOptions::S3DefaultObjectMetaRequestOptions(
                const std::shared_ptr<Http::HttpRequest> &request,
                const Crt::String &operationName) noexcept
                : S3MetaRequestOptions(S3MetaRequestType::Default, request)
            {
                m_operationName = operationName;
                m_impl->options.operation_name = ByteCursorFromString(m_operationName);
            }

            ScopedResource<S3MetaRequestOptions> S3DefaultObjectMetaRequestOptions::Create(
                const std::shared_ptr<Http::HttpRequest> &request,
                const Crt::String &operationName) noexcept
            {
                return s_makeOptions<S3DefaultObjectMetaRequestOptions>(request, operationName);
            }

            struct S3MetaRequestCallbackData
            {
                std::shared_ptr<S3MetaRequest> wrapper;
                S3MetaRequestOptions::BodyCallback bodyCb;
                S3MetaRequestOptions::BodyCallbackEx bodyCbEx;
                S3MetaRequestOptions::HeadersCallback headersCb;
                S3MetaRequestOptions::ProgressCallback progressCb;
                S3MetaRequestOptions::FinishCallback finishCb;
                S3MetaRequestOptions::ShutdownCallback shutdownCb;
            };

            static Vector<Http::HttpHeader> s_materializeHeaders(const struct aws_http_headers *headers) noexcept
            {
                Vector<Http::HttpHeader> out;
                if (headers == nullptr)
                {
                    return out;
                }
                const size_t count = aws_http_headers_count(headers);
                out.reserve(count);
                for (size_t i = 0; i < count; ++i)
                {
                    Http::HttpHeader header;
                    if (aws_http_headers_get_index(headers, i, &header) == AWS_OP_SUCCESS)
                    {
                        out.emplace_back(header);
                    }
                }
                return out;
            }

            static int s_onHeaders(
                struct aws_s3_meta_request * /*meta*/,
                const struct aws_http_headers *headers,
                int responseStatus,
                void *user_data)
            {
                auto *data = static_cast<S3MetaRequestCallbackData *>(user_data);
                if (!data->headersCb)
                {
                    return AWS_OP_SUCCESS;
                }
                return data->headersCb(s_materializeHeaders(headers), responseStatus);
            }

            static int s_onBody(
                struct aws_s3_meta_request * /*meta*/,
                const ByteCursor *body,
                uint64_t rangeStart,
                void *user_data)
            {
                auto *data = static_cast<S3MetaRequestCallbackData *>(user_data);
                return data->bodyCb ? data->bodyCb(*body, rangeStart) : AWS_OP_SUCCESS;
            }

            static int s_onBodyEx(
                struct aws_s3_meta_request * /*meta*/,
                const ByteCursor *body,
                const struct aws_s3_meta_request_receive_body_extra_info info,
                void *user_data)
            {
                auto *data = static_cast<S3MetaRequestCallbackData *>(user_data);
                if (!data->bodyCbEx)
                {
                    return AWS_OP_SUCCESS;
                }
                // Stack-only, non-owning wrapper over the CRT's borrowed ticket
                // (which may be null, ex. HEAD_OBJECT): no heap allocation and no
                // reference taken on the per-chunk path. If the callback needs the
                // buffer past its return, it calls ticket.Acquire(), which is the
                // only path that takes a reference and allocates.
                S3BufferTicket ticket(info.ticket);
                return data->bodyCbEx(*body, info.range_start, ticket);
            }

            static void s_onProgress(
                struct aws_s3_meta_request * /*meta*/,
                const struct aws_s3_meta_request_progress *progress,
                void *user_data)
            {
                auto *data = static_cast<S3MetaRequestCallbackData *>(user_data);
                if (data->progressCb)
                {
                    data->progressCb(progress->bytes_transferred, progress->content_length);
                }
            }

            static void s_onFinish(
                struct aws_s3_meta_request * /*meta*/,
                const struct aws_s3_meta_request_result *result,
                void *user_data)
            {
                auto *data = static_cast<S3MetaRequestCallbackData *>(user_data);
                if (data->finishCb)
                {
                    S3MetaRequestResult cppResult{};
                    cppResult.errorCode = result->error_code;
                    cppResult.responseStatus = result->response_status;
                    cppResult.errorResponseHeaders = s_materializeHeaders(result->error_response_headers);
                    // Borrowed view over the CRT-owned buffer (freed when this
                    // callback returns); empty cursor when there is no body.
                    cppResult.errorResponseBody = result->error_response_body != nullptr
                                                      ? ByteCursorFromByteBuf(*result->error_response_body)
                                                      : ByteCursor{0, nullptr};
                    cppResult.didValidateChecksum = result->did_validate;
                    switch (result->validation_algorithm)
                    {
                        case AWS_SCA_NONE:
                            cppResult.validationAlgorithm = S3ChecksumAlgorithm::None;
                            break;
                        case AWS_SCA_CRC32C:
                            cppResult.validationAlgorithm = S3ChecksumAlgorithm::Crc32c;
                            break;
                        case AWS_SCA_CRC32:
                            cppResult.validationAlgorithm = S3ChecksumAlgorithm::Crc32;
                            break;
                        case AWS_SCA_SHA1:
                            cppResult.validationAlgorithm = S3ChecksumAlgorithm::Sha1;
                            break;
                        case AWS_SCA_SHA256:
                            cppResult.validationAlgorithm = S3ChecksumAlgorithm::Sha256;
                            break;
                        case AWS_SCA_CRC64NVME:
                            cppResult.validationAlgorithm = S3ChecksumAlgorithm::Crc64Nvme;
                            break;
                        case AWS_SCA_SHA512:
                            cppResult.validationAlgorithm = S3ChecksumAlgorithm::Sha512;
                            break;
                        case AWS_SCA_XXHASH64:
                            cppResult.validationAlgorithm = S3ChecksumAlgorithm::XXHash64;
                            break;
                        case AWS_SCA_XXHASH3_64:
                            cppResult.validationAlgorithm = S3ChecksumAlgorithm::XXHash3_64;
                            break;
                        case AWS_SCA_XXHASH3_128:
                            cppResult.validationAlgorithm = S3ChecksumAlgorithm::XXHash3_128;
                            break;
                        default:
                            cppResult.validationAlgorithm = S3ChecksumAlgorithm::None;
                            break;
                    }
                    data->finishCb(cppResult);
                }
            }

            static void s_onShutdown(void *user_data)
            {
                auto *data = static_cast<S3MetaRequestCallbackData *>(user_data);
                if (data->shutdownCb)
                {
                    data->shutdownCb();
                }
                Delete(data, ApiAllocator());
            }

            // CRT client shutdown is async and outlives the S3Client object,
            // so the callback bundle is heap-allocated and self-frees in the
            // trampoline.
            struct S3ClientShutdownCallbackData
            {
                std::function<void()> callback;
            };

            static void s_onClientShutdown(void *user_data)
            {
                auto *data = static_cast<S3ClientShutdownCallbackData *>(user_data);
                if (data->callback)
                {
                    data->callback();
                }
                Delete(data, ApiAllocator());
            }

            S3Client::S3Client(const S3ClientConfig &config) noexcept : m_lastError(AWS_ERROR_SUCCESS)
            {
                Allocator *allocator = ApiAllocator();
                struct aws_s3_client_config *rawConfig = config.GetUnderlyingHandle();

                // aws-c-s3 mandates a non-null client_bootstrap; fall back to
                // the process-global default when the caller didn't set one.
                if (rawConfig->client_bootstrap == nullptr)
                {
                    Io::ClientBootstrap *defaultBootstrap = ApiHandle::GetOrCreateStaticDefaultClientBootstrap();
                    if (defaultBootstrap != nullptr)
                    {
                        rawConfig->client_bootstrap = defaultBootstrap->GetUnderlyingHandle();
                    }
                }

                // Materialize the retry strategy now that the event loop
                // group is settled. Factory path wins; Default leaves it null
                // for the CRT to build its own. aws_s3_client_new acquires
                // its own ref, so this binding may drop after construction.
                Optional<S3RetryStrategy> retryStrategy;
                if (config.GetRetryStrategyFactory())
                {
                    retryStrategy = config.GetRetryStrategyFactory()(config);
                }
                else
                {
                    switch (config.GetRetryStrategyType())
                    {
                        case S3RetryStrategyType::Standard:
                            retryStrategy = S3RetryStrategy::CreateStandard();
                            break;
                        case S3RetryStrategyType::ExponentialBackoff:
                        {
                            // Enum path has no EventLoopGroup handle to hand
                            // to the factory; source it from the bootstrap.
                            const auto &opts = config.GetRetryStrategyOptions();
                            struct aws_exponential_backoff_retry_options backoffOptions;
                            AWS_ZERO_STRUCT(backoffOptions);
                            if (rawConfig->client_bootstrap != nullptr)
                            {
                                backoffOptions.el_group = rawConfig->client_bootstrap->event_loop_group;
                            }
                            backoffOptions.max_retries = opts.maxRetries;
                            backoffOptions.backoff_scale_factor_ms = opts.scaleFactorMs;
                            backoffOptions.max_backoff_secs = opts.maxBackoffSecs;
                            backoffOptions.jitter_mode = AWS_EXPONENTIAL_BACKOFF_JITTER_FULL;
                            retryStrategy =
                                S3RetryStrategy(aws_retry_strategy_new_exponential_backoff(allocator, &backoffOptions));
                            break;
                        }
                        case S3RetryStrategyType::NoRetry:
                            retryStrategy = S3RetryStrategy::CreateNoRetry();
                            break;
                        case S3RetryStrategyType::Default:
                            break;
                    }
                }
                if (retryStrategy && retryStrategy->GetUnderlyingHandle() != nullptr)
                {
                    rawConfig->retry_strategy = retryStrategy->GetUnderlyingHandle();
                }

                // Local cursor array; aws_s3_client_new deep-copies it.
                const Vector<String> &networkInterfaceNames = config.GetNetworkInterfaceNames();
                Vector<aws_byte_cursor> networkInterfaceNameCursors;
                if (!networkInterfaceNames.empty())
                {
                    networkInterfaceNameCursors.reserve(networkInterfaceNames.size());
                    for (const auto &name : networkInterfaceNames)
                    {
                        networkInterfaceNameCursors.push_back(ByteCursorFromString(name));
                    }
                    rawConfig->network_interface_names_array = networkInterfaceNameCursors.data();
                    rawConfig->num_network_interface_names = networkInterfaceNameCursors.size();
                }

                // Bundle is freed by s_onClientShutdown, which the CRT
                // fires only on successful client creation.
                S3ClientShutdownCallbackData *shutdownData = nullptr;
                if (config.GetClientShutdownCallback())
                {
                    shutdownData = New<S3ClientShutdownCallbackData>(allocator);
                    shutdownData->callback = config.GetClientShutdownCallback();
                    rawConfig->shutdown_callback = s_onClientShutdown;
                    rawConfig->shutdown_callback_user_data = shutdownData;
                }

                m_client = ScopedResource<struct aws_s3_client>(
                    aws_s3_client_new(allocator, rawConfig), aws_s3_client_release);
                m_lastError = m_client ? AWS_ERROR_SUCCESS : aws_last_error();

                // aws_s3_client_new deep-copies what it needs, so clear the fields
                // that were pointed at constructor-locals (the retry strategy and
                // the network-interface cursor array). Otherwise the caller's
                // config would be left holding dangling pointers into freed stack
                // storage, which would be read if it were reused to build another
                // client.
                rawConfig->retry_strategy = nullptr;
                rawConfig->network_interface_names_array = nullptr;
                rawConfig->num_network_interface_names = 0;

                // On failure the CRT won't invoke the shutdown callback; free
                // the bundle here to avoid leaking it.
                if (m_client == nullptr && shutdownData != nullptr)
                {
                    Delete(shutdownData, allocator);
                }
            }

            int S3Client::LastError() const noexcept
            {
                return m_lastError ? m_lastError : AWS_ERROR_UNKNOWN;
            }

            bool S3Client::MakeDefaultSigningConfig(
                Auth::AwsSigningConfig &config,
                const String &region,
                const std::shared_ptr<Auth::ICredentialsProvider> &provider) noexcept
            {
                // aws_s3_init_default_signing_config's precondition aborts
                // under DEBUG_BUILD if the provider is null.
                if (provider == nullptr)
                {
                    return false;
                }

                struct aws_signing_config_aws raw;
                AWS_ZERO_STRUCT(raw);
                aws_s3_init_default_signing_config(&raw, ByteCursorFromString(region), provider->GetUnderlyingHandle());

                config.SetSigningAlgorithm(static_cast<Auth::SigningAlgorithm>(raw.algorithm));
                config.SetSignatureType(static_cast<Auth::SignatureType>(raw.signature_type));
                config.SetRegion(region);
                config.SetService(String(reinterpret_cast<const char *>(raw.service.ptr), raw.service.len));
                config.SetSignedBodyHeader(static_cast<Auth::SignedBodyHeaderType>(raw.signed_body_header));
                config.SetSignedBodyValue(
                    String(reinterpret_cast<const char *>(raw.signed_body_value.ptr), raw.signed_body_value.len));
                config.SetCredentialsProvider(provider);

                // S3 requires single URI encoding and no path normalization
                // or requests for keys with reserved characters fail with
                // SignatureDoesNotMatch. The C++ ctor defaults both to true.
                config.SetUseDoubleUriEncode(false);
                config.SetShouldNormalizeUriPath(false);
                return true;
            }

            std::shared_ptr<S3MetaRequest> S3Client::MakeMetaRequest(S3MetaRequestOptions &options) noexcept
            {
                if (!*this)
                {
                    m_lastError = AWS_ERROR_INVALID_STATE;
                    return nullptr;
                }

                if (options.GetBodyCallback() && options.GetBodyCallbackEx())
                {
                    m_lastError = AWS_ERROR_INVALID_ARGUMENT;
                    return nullptr;
                }

                // Surface a validation error deferred by a Set* setter (ex. an
                // invalid endpoint URI) rather than issuing a malformed request.
                if (options.GetLastError() != AWS_ERROR_SUCCESS)
                {
                    m_lastError = options.GetLastError();
                    return nullptr;
                }

                Allocator *allocator = ApiAllocator();
                auto *callbackData = New<S3MetaRequestCallbackData>(allocator);
                // Copy (not move) the callbacks out: the getters expose them as
                // const refs so the options object cannot be left in a half-moved
                // state, and copying a std::function is cheap.
                callbackData->bodyCb = options.GetBodyCallback();
                callbackData->bodyCbEx = options.GetBodyCallbackEx();
                callbackData->headersCb = options.GetHeadersCallback();
                callbackData->progressCb = options.GetProgressCallback();
                callbackData->finishCb = options.GetFinishCallback();
                callbackData->shutdownCb = options.GetShutdownCallback();

                auto wrapper = Aws::Crt::MakeShared<S3MetaRequest>(allocator);
                callbackData->wrapper = wrapper;

                struct aws_s3_meta_request_options *rawOptions = options.GetUnderlyingHandle();
                rawOptions->user_data = callbackData;
                rawOptions->headers_callback = s_onHeaders;
                rawOptions->body_callback = callbackData->bodyCb ? s_onBody : nullptr;
                rawOptions->body_callback_ex = callbackData->bodyCbEx ? s_onBodyEx : nullptr;
                rawOptions->progress_callback = s_onProgress;
                rawOptions->finish_callback = s_onFinish;
                rawOptions->shutdown_callback = s_onShutdown;

                struct aws_s3_meta_request *rawHandle = aws_s3_client_make_meta_request(m_client.get(), rawOptions);
                if (rawHandle == nullptr)
                {
                    m_lastError = aws_last_error();
                    Delete(callbackData, allocator);
                    return nullptr;
                }
                wrapper->SetUnderlyingHandle(rawHandle);
                return wrapper;
            }

            S3MetaRequest::S3MetaRequest() noexcept : m_lastError(AWS_ERROR_SUCCESS) {}

            void S3MetaRequest::SetUnderlyingHandle(struct aws_s3_meta_request *handle) noexcept
            {
                m_metaRequest = ScopedResource<struct aws_s3_meta_request>(handle, aws_s3_meta_request_release);
            }

            void S3MetaRequest::Cancel() noexcept
            {
                aws_s3_meta_request_cancel(m_metaRequest.get());
            }

            void S3MetaRequest::IncrementReadWindow(uint64_t bytes) noexcept
            {
                aws_s3_meta_request_increment_read_window(m_metaRequest.get(), bytes);
            }

            // Bridges the aws_future_void returned by aws_s3_meta_request_write to a
            // std::promise<int>. Heap-allocated so it outlives Write(); freed in the
            // completion trampoline.
            struct S3MetaRequestWriteData
            {
                std::promise<int> WritePromise;
                struct aws_future_void *WriteFuture = nullptr;

                static void OnWriteComplete(void *userData)
                {
                    auto writeData = static_cast<S3MetaRequestWriteData *>(userData);
                    writeData->WritePromise.set_value(aws_future_void_get_error(writeData->WriteFuture));
                    aws_future_void_release(writeData->WriteFuture);
                    Aws::Crt::Delete(writeData, ApiAllocator());
                }
            };

            std::future<int> S3MetaRequest::Write(ByteCursor data, bool eof) noexcept
            {
                auto *writeData = Aws::Crt::New<S3MetaRequestWriteData>(ApiAllocator());
                if (writeData == nullptr)
                {
                    std::promise<int> failed;
                    failed.set_value(aws_last_error());
                    return failed.get_future();
                }

                auto future = writeData->WritePromise.get_future();
                writeData->WriteFuture = aws_s3_meta_request_write(m_metaRequest.get(), data, eof);
                aws_future_void_register_callback(
                    writeData->WriteFuture, S3MetaRequestWriteData::OnWriteComplete, writeData);
                return future;
            }

            int S3MetaRequest::LastError() const noexcept
            {
                return m_lastError ? m_lastError : AWS_ERROR_UNKNOWN;
            }

        } // namespace S3
    } // namespace Crt
} // namespace Aws
