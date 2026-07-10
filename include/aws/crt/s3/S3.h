#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Exports.h>
#include <aws/crt/Optional.h>
#include <aws/crt/Types.h>
#include <aws/crt/auth/Sigv4Signing.h>
#include <aws/crt/http/HttpConnection.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/crt/io/TlsOptions.h>
#include <aws/crt/io/Uri.h>
#include <aws/crt/s3/S3BufferTicket.h>

#include <cstdint>
#include <functional>
#include <future>
#include <memory>

struct aws_s3_client;
struct aws_s3_client_config;
struct aws_s3_meta_request;
struct aws_s3_meta_request_options;
struct aws_retry_strategy;

namespace Aws
{
    namespace Crt
    {
        namespace Auth
        {
            class ICredentialsProvider;
        }

        namespace S3
        {
            /**
             * The kind of S3 operation that a meta request will perform.
             * Default is a single-request pass-through; the other values enable the
             * CRT's multi-part orchestration for the named operation.
             */
            // Mapped to their aws-c-s3 counterparts by a switch in S3.cpp.
            enum class S3MetaRequestType
            {
                Default,
                GetObject,
                PutObject,
                CopyObject,
            };

            /**
             * Where a calculated request-side checksum is placed on the wire: None adds
             * no payload checksum, Header puts the checksum in the request headers, and
             * Trailer aws-chunked-encodes the payload and puts the checksum in the trailer.
             */
            enum class S3ChecksumLocation
            {
                None,
                Header,
                Trailer,
            };

            /**
             * The checksum algorithm used for request-side checksum calculation or
             * response validation. A subset of the algorithms aws-c-s3 supports.
             */
            enum class S3ChecksumAlgorithm
            {
                None,
                Crc32c,
                Crc32,
                Sha1,
                Sha256,
                Crc64Nvme,
                Sha512,
                XXHash64,
                XXHash3_64,
                XXHash3_128,
            };

            /**
             * Whether the client establishes connections over TLS. The
             * client only consults the configured TLS connection options when
             * the mode is Enabled.
             */
            enum class S3TlsMode
            {
                Enabled,
                Disabled,
            };

            /**
             * How S3MetaRequestOptions::SetRecvFilepath opens the destination
             * file. Mirrors aws_s3_recv_file_options. Only meaningful when a
             * receive filepath is configured; the enum values are mapped to the
             * C enum by a switch in S3.cpp.
             */
            enum class S3RecvFileMode
            {
                /** Create the file, or replace it if it already exists. */
                CreateOrReplace,
                /** Always create a new file; fail if the path already exists. */
                CreateNew,
                /** Create a new file if it doesn't exist; otherwise append. */
                CreateOrAppend,
                /**
                 * Write to an existing file at the position supplied via
                 * SetRecvFilePosition; the file must already exist.
                 */
                WriteToPosition,
            };

            /**
             * Which network-level retry strategy the S3 client uses. Mirrors the
             * flavors aws-c-io exposes. Default leaves the choice to the CRT,
             * which builds its own standard strategy when none is supplied.
             */
            enum class S3RetryStrategyType
            {
                /** Leave retry_strategy unset; the CRT builds its default strategy. */
                Default,
                /** Token-bucket standard strategy (aws_retry_strategy_new_standard). */
                Standard,
                /** Exponential-backoff strategy (aws_retry_strategy_new_exponential_backoff). */
                ExponentialBackoff,
                /** Disable retries (aws_retry_strategy_new_no_retry). */
                NoRetry,
            };

            /**
             * Tuning knobs for S3RetryStrategyType::ExponentialBackoff (the parameter
             * type of CreateExponentialBackoff and the options argument to
             * SetRetryStrategy). These knobs are applied only to the
             * exponential-backoff strategy; they are ignored for the Standard and
             * NoRetry flavors.
             *
             * NOTE: maxRetries is a count, not an on/off switch. A value of 0 does
             * NOT disable retries -- aws-c-io treats 0 as "unset" and substitutes
             * its own default (currently 5). To disable retries entirely, select
             * S3RetryStrategyType::NoRetry rather than setting maxRetries to 0.
             */
            struct AWS_CRT_CPP_API S3RetryStrategyExponentialBackoffOptions
            {
                /**
                 * Maximum number of retries (1..63). 0 is treated as "unset" and
                 * uses the aws-c-io default (currently 5), not zero retries; for
                 * zero retries use S3RetryStrategyType::NoRetry.
                 */
                size_t maxRetries = 0;
                /** Scaling factor for the backoff, in milliseconds. */
                uint32_t scaleFactorMs = 500;
                /** Maximum delay between retries, in seconds. */
                uint32_t maxBackoffSecs = 20;
            };

            /**
             * C++ binding over a CRT aws_retry_strategy handle. Owns the handle and
             * releases it on destruction; construct one with the static Create*
             * factories. Hand the result to S3ClientConfig::SetRetryStrategy or
             * return it from a SetRetryStrategyFactory callback - the S3 client
             * acquires its own reference at construction, so the binding may be
             * destroyed afterward.
             *
             * Move-only: the underlying handle has single-owner semantics here.
             */
            class AWS_CRT_CPP_API S3RetryStrategy final
            {
              public:
                S3RetryStrategy(const S3RetryStrategy &) = delete;
                S3RetryStrategy &operator=(const S3RetryStrategy &) = delete;
                S3RetryStrategy(S3RetryStrategy &&other) noexcept = default;
                S3RetryStrategy &operator=(S3RetryStrategy &&other) noexcept = default;
                ~S3RetryStrategy() noexcept = default;

                /**
                 * Build a token-bucket standard retry strategy.
                 *
                 * @return a binding owning the new strategy, or an empty binding on failure.
                 */
                static S3RetryStrategy CreateStandard() noexcept;

                /**
                 * Build an exponential-backoff retry strategy. Requires an event
                 * loop group to schedule the backoff timers on.
                 *
                 * @param elGroup event loop group used to schedule retries.
                 * @param options backoff tuning knobs.
                 * @return a binding owning the new strategy, or an empty binding on failure.
                 */
                static S3RetryStrategy CreateExponentialBackoff(
                    Io::EventLoopGroup &elGroup,
                    const S3RetryStrategyExponentialBackoffOptions &options = {}) noexcept;

                /**
                 * Build a strategy that disables retries.
                 *
                 * @return a binding owning the new strategy, or an empty binding on failure.
                 */
                static S3RetryStrategy CreateNoRetry() noexcept;

                /**
                 * @return true if this binding owns a valid strategy handle.
                 */
                explicit operator bool() const noexcept { return m_strategy != nullptr; }

                /// @private
                /// Takes ownership of an already-created handle (or nullptr).
                explicit S3RetryStrategy(aws_retry_strategy *strategy) noexcept;

                /// @private
                aws_retry_strategy *GetUnderlyingHandle() const noexcept { return m_strategy.get(); }

              private:
                ScopedResource<struct aws_retry_strategy> m_strategy;
            };

            class S3Client;
            class S3MetaRequest;
            class S3MetaRequestOptions;

            /**
             * Result delivered to FinishCallback when a meta request terminates.
             * Mirrors aws_s3_meta_request_result. errorResponseHeaders is a deep copy
             * you may retain freely. errorResponseBody is a borrowed view into
             * CRT-owned memory, valid only for the duration of the FinishCallback
             * invocation; copy out its contents if you need to retain them.
             */
            struct AWS_CRT_CPP_API S3MetaRequestResult
            {
                /** AWS_ERROR_SUCCESS on success, a CRT error code otherwise. */
                int errorCode;

                /** HTTP status of the request (the successful response on success, or the failed request on an S3 error
                 * response); 0 for other error codes. */
                int responseStatus;

                /** Headers of the S3 error response. Empty if not applicable. */
                Vector<Http::HttpHeader> errorResponseHeaders;

                /**
                 * Bytes of the S3 error response body, or an empty cursor if not
                 * applicable. Borrowed: the underlying buffer is owned by the meta
                 * request and freed when the FinishCallback returns, so copy the
                 * bytes out if you need to retain them.
                 */
                ByteCursor errorResponseBody;

                /** True if the server-side checksum was validated against a calculated value. */
                bool didValidateChecksum;

                /** Algorithm used for checksum validation, when didValidateChecksum is true. */
                S3ChecksumAlgorithm validationAlgorithm;
            };

            /**
             * Per-meta-request checksum configuration. The same object is used to
             * describe request-side checksum calculation for uploads and
             * response-side checksum validation for downloads; not every field
             * applies in both directions.
             */
            class AWS_CRT_CPP_API S3ChecksumConfig final
            {
              public:
                S3ChecksumConfig() noexcept;

                /**
                 * Set where a calculated checksum will appear on the wire.
                 *
                 * @param location the wire location for the request-side checksum.
                 * @return this object, to allow chaining.
                 */
                S3ChecksumConfig &SetLocation(S3ChecksumLocation location) noexcept
                {
                    m_location = location;
                    return *this;
                }

                /**
                 * Set the algorithm used to compute checksums for this request.
                 *
                 * @param algorithm the checksum algorithm to use.
                 * @return this object, to allow chaining.
                 */
                S3ChecksumConfig &SetChecksumAlgorithm(S3ChecksumAlgorithm algorithm) noexcept
                {
                    m_algorithm = algorithm;
                    return *this;
                }

                /**
                 * Enable or disable validation of the response-side checksum.
                 * Only meaningful for downloads.
                 *
                 * @param validate true to enable response checksum validation.
                 * @return this object, to allow chaining.
                 */
                S3ChecksumConfig &SetValidateResponseChecksum(bool validate) noexcept
                {
                    m_validateResponseChecksum = validate;
                    return *this;
                }

                /**
                 * @return the configured wire location for the request-side checksum.
                 */
                S3ChecksumLocation GetLocation() const noexcept { return m_location; }

                /**
                 * @return the configured checksum algorithm.
                 */
                S3ChecksumAlgorithm GetChecksumAlgorithm() const noexcept { return m_algorithm; }

                /**
                 * @return whether response-side checksum validation is enabled.
                 */
                bool GetValidateResponseChecksum() const noexcept { return m_validateResponseChecksum; }

              private:
                S3ChecksumLocation m_location;
                S3ChecksumAlgorithm m_algorithm;
                bool m_validateResponseChecksum;
            };

            /**
             * Configuration object used to construct an S3Client. The credentials
             * provider is required at construction; every other setting is optional
             * and set through the fluent Set* setters. The object owns backing
             * storage for string-valued fields, the credentials provider reference,
             * and the internally-built signing config, so it must outlive the
             * S3Client construction call.
             */
            class AWS_CRT_CPP_API S3ClientConfig final
            {
              public:
                S3ClientConfig(const S3ClientConfig &) = delete;
                S3ClientConfig(S3ClientConfig &&) = delete;
                S3ClientConfig &operator=(const S3ClientConfig &) = delete;
                S3ClientConfig &operator=(S3ClientConfig &&) = delete;

                /**
                 * Construct a config. The credentials provider is required (no
                 * defensible default - the SDK can't guess credentials). The
                 * signing config is built internally via
                 * S3Client::MakeDefaultSigningConfig, which wraps
                 * aws_s3_init_default_signing_config and additionally clears
                 * use_double_uri_encode and should_normalize_uri_path (both
                 * required for S3 request signing; without either, keys with
                 * reserved characters or "//" fail with SignatureDoesNotMatch).
                 * Region defaults to "us-east-1"; override via SetRegion.
                 * S3 Express is enabled by default; override via
                 * SetEnableS3Express.
                 *
                 * @param credentialsProvider the credentials provider used to
                 *        source SigV4 credentials. The config retains a shared
                 *        reference; the provider must remain valid at least
                 *        until the S3Client is constructed (aws_s3_client_new
                 *        deep-copies the signing config and acquires its own
                 *        reference to the provider).
                 */
                explicit S3ClientConfig(
                    const std::shared_ptr<Auth::ICredentialsProvider> &credentialsProvider) noexcept;

                // Defined out-of-line in S3.cpp where Impl is a complete type
                // (required to destroy the ScopedResource<Impl> pImpl member).
                ~S3ClientConfig() noexcept;

                /**
                 * Override the AWS region the client signs requests for. Defaults
                 * to "us-east-1" if not set.
                 *
                 * @param region the region string, ex. "us-east-1".
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetRegion(const Crt::String &region) noexcept;

                /**
                 * Set the target aggregate throughput the client will tune toward,
                 * in gigabits per second. This drives the CRT's pool sizing.
                 *
                 * @param gbps target throughput in Gbps.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetThroughputTargetGbps(double gbps) noexcept;

                /**
                 * Set the target part size for multipart transfers (downloads and
                 * uploads). For uploads, the CRT may increase the effective part size
                 * if the target would require more than 10,000 parts (the S3 service
                 * limit), and will also raise it to the 5 MiB minimum upload part size.
                 *
                 * @param bytes target part size in bytes.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetPartSize(uint64_t bytes) noexcept;

                /**
                 * @param bytes threshold in bytes above which an upload is
                 *        split into a multipart upload.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetMultipartUploadThreshold(uint64_t bytes) noexcept;

                /**
                 * Set the upper bound on memory the CRT may buffer for in-flight
                 * parts across all meta requests created with this client.
                 *
                 * @param bytes memory ceiling in bytes.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetMemoryLimit(uint64_t bytes) noexcept;

                /**
                 * Set the client bootstrap used for connection establishment. If
                 * not set, the process-global default bootstrap is used (see
                 * Aws::Crt::ApiHandle::GetOrCreateStaticDefaultClientBootstrap).
                 *
                 * @param bootstrap the client bootstrap.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetClientBootstrap(Io::ClientBootstrap &bootstrap) noexcept;

                /**
                 * @param timeoutMs connection timeout in milliseconds.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetConnectTimeoutMs(uint32_t timeoutMs) noexcept;

                /**
                 * Enable read backpressure and set the initial read window, in
                 * bytes, for downloads. When backpressure is enabled the CRT pauses
                 * delivering body data once the window is exhausted until the
                 * application increments it. A window of 0 with backpressure
                 * enabled means no data is read until the window is incremented.
                 *
                 * @param enable whether to enable read backpressure.
                 * @param initialReadWindow initial read window in bytes.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetReadBackpressure(bool enable, uint64_t initialReadWindow) noexcept;

                /**
                 * Enable or disable S3 Express support. Enabled by default to
                 * match the AWS SDK S3 client. When enabled and no provider
                 * factory is set, the CRT creates a default S3 Express provider.
                 *
                 * @param enable whether to enable S3 Express support.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetEnableS3Express(bool enable) noexcept;

                /**
                 * Set whether the client establishes connections over TLS. The
                 * configured TLS connection options are only consulted when the
                 * mode is Enabled.
                 *
                 * @param mode the TLS mode.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetTlsMode(S3TlsMode mode) noexcept;

                /**
                 * Set the TLS connection options used for connections. Pair with
                 * SetTlsMode(S3TlsMode::Enabled); the options are ignored when TLS
                 * mode is Disabled. The config owns a copy of the options.
                 *
                 * @param options the TLS connection options.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetTlsConnectionOptions(const Io::TlsConnectionOptions &options) noexcept;

                /**
                 * Set the HTTP proxy options used for connections. The config owns
                 * a copy of the options.
                 *
                 * @param proxyOptions the proxy options.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetProxyOptions(const Http::HttpClientConnectionProxyOptions &proxyOptions) noexcept;

                /**
                 * Configure TCP keep-alive for connections. Both values are in
                 * seconds; a value of 0 leaves the corresponding setting at the
                 * operating-system default.
                 *
                 * @param keepAliveIntervalSec interval between keep-alive probes.
                 * @param keepAliveTimeoutSec timeout before a connection is considered dead.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetTcpKeepAlive(uint16_t keepAliveIntervalSec, uint16_t keepAliveTimeoutSec) noexcept;

                /**
                 * Enable connection health monitoring. A connection is shut down if
                 * its throughput falls below the minimum for longer than the
                 * allowable failure interval.
                 *
                 * @param minimumThroughputBytesPerSecond minimum acceptable throughput.
                 * @param allowableThroughputFailureIntervalSeconds grace interval in seconds.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetConnectionMonitoring(
                    uint64_t minimumThroughputBytesPerSecond,
                    uint32_t allowableThroughputFailureIntervalSeconds) noexcept;

                /**
                 * Set the network interface names the client distributes its
                 * connections across, allowing it to saturate multiple NICs. If
                 * any interface name is invalid or its link goes down, you will
                 * see connection failures.
                 *
                 * Experimental and only supported on Linux, macOS, and platforms
                 * with SO_BINDTODEVICE or IP_BOUND_IF; not supported on Windows
                 * (AWS_ERROR_PLATFORM_NOT_SUPPORTED is raised on unsupported
                 * platforms). The config owns a copy of the names.
                 *
                 * @param networkInterfaceNames the interface names, ex. "eth0".
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetNetworkInterfaceNames(const Vector<String> &networkInterfaceNames) noexcept;

                /**
                 * Select a network-level retry strategy by flavor. This is the
                 * common path: the matching aws_retry_strategy is built when the
                 * S3Client is constructed (the point at which the event loop group
                 * is known). S3RetryStrategyType::Default leaves the choice to the
                 * CRT. Overrides any factory set via SetRetryStrategyFactory.
                 *
                 * @param type the retry strategy flavor.
                 * @param options backoff knobs, used only for ExponentialBackoff.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetRetryStrategy(
                    S3RetryStrategyType type,
                    const S3RetryStrategyExponentialBackoffOptions &options = {}) noexcept;

                /**
                 * Install a factory for fine-grained control over the retry
                 * strategy. Invoked once, at S3Client construction, with this
                 * config; it returns the S3RetryStrategy binding the client should
                 * use. Use this when the flavors exposed by SetRetryStrategy are
                 * not sufficient. Overrides any flavor set via SetRetryStrategy.
                 *
                 * @param factory callback that produces the retry strategy.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetRetryStrategyFactory(
                    std::function<S3RetryStrategy(const S3ClientConfig &)> factory) noexcept;

                /**
                 * Install a callback invoked once the underlying CRT client has
                 * finished its asynchronous shutdown. aws_s3_client_release (called
                 * from ~S3Client) only starts teardown; the client's threads and
                 * connection pool may still be winding down after the destructor
                 * returns. This callback is the signal that teardown is complete and
                 * it is safe to release dependencies the client borrowed (event loop
                 * group, client bootstrap, credentials provider). It fires on a CRT
                 * thread, after the S3Client object may already be gone.
                 *
                 * @param callback the callback to invoke on shutdown completion.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &SetClientShutdownCallback(std::function<void()> callback) noexcept;

                /// @private
                /// Raw handle for the C layer (aws_s3_client_new); not part of the
                /// public API.
                struct aws_s3_client_config *GetUnderlyingHandle() const noexcept;

                /**
                 * @return the configured retry-strategy flavor (Default if unset).
                 */
                S3RetryStrategyType GetRetryStrategyType() const noexcept { return m_retryStrategyType; }

                /**
                 * @return the exponential-backoff tuning knobs; only meaningful when
                 *         the strategy type is ExponentialBackoff.
                 */
                const S3RetryStrategyExponentialBackoffOptions &GetRetryStrategyOptions() const noexcept
                {
                    return m_retryStrategyOptions;
                }

                /**
                 * @return the retry-strategy factory, or an empty function if none
                 *         was installed via SetRetryStrategyFactory.
                 */
                const std::function<S3RetryStrategy(const S3ClientConfig &)> &GetRetryStrategyFactory() const noexcept
                {
                    return m_retryStrategyFactory;
                }

                /**
                 * @return the configured network interface names, or an empty vector
                 *         if none were set.
                 */
                const Vector<String> &GetNetworkInterfaceNames() const noexcept { return m_networkInterfaceNames; }

                /**
                 * @return the client-shutdown callback, or an empty function if none
                 *         was installed via SetClientShutdownCallback.
                 */
                const std::function<void()> &GetClientShutdownCallback() const noexcept
                {
                    return m_clientShutdownCallback;
                }

              private:
                // The CRT C-struct storage (aws_s3_client_config plus the proxy,
                // TCP keep-alive, and connection-monitoring option backings) lives
                // by value inside Impl, defined in S3.cpp. This keeps the C headers
                // out of this public header and collapses what were four separate
                // heap allocations into the single Impl allocation.
                struct Impl;
                ScopedResource<Impl> m_impl;
                String m_region;
                Optional<Io::TlsConnectionOptions> m_tlsConnectionOptions;
                Optional<Http::HttpClientConnectionProxyOptions> m_proxyOptions;
                Vector<String> m_networkInterfaceNames;

                // Strategy is materialized at S3Client construction; the
                // factory (when set) takes precedence over the enum.
                S3RetryStrategyType m_retryStrategyType;
                S3RetryStrategyExponentialBackoffOptions m_retryStrategyOptions;
                std::function<S3RetryStrategy(const S3ClientConfig &)> m_retryStrategyFactory;

                std::function<void()> m_clientShutdownCallback;
                std::shared_ptr<Auth::ICredentialsProvider> m_credentialsProvider;
                // Held by value (no heap): single-owner, scope-bound to this
                // config. Optional carries the "no signing config" state the
                // ctor relies on when no credentials provider is supplied.
                Optional<Auth::AwsSigningConfig> m_signingConfig;
            };

            /**
             * Abstract base for the four concrete options types. Users obtain
             * a unique_ptr through the subclass's static Create factory (some
             * subclasses overload Create so the body sink or source is chosen
             * by argument type), then set cross-cutting fields on the base
             * through the shared Set* setters before handing the object to
             * S3Client::MakeMetaRequest.
             *
             * Mutual exclusion of body-delivery paths (callback / callback_ex
             * / recv_filepath) is enforced structurally by the factory shapes.
             */
            class AWS_CRT_CPP_API S3MetaRequestOptions
            {
              public:
                /**
                 * Invoked once per delivered body chunk during a download.
                 * @param body the chunk of object bytes for this delivery.
                 * @param rangeStart byte offset within the object that this chunk starts at.
                 * @return AWS_OP_SUCCESS to continue, or an error code to abort the meta request.
                 */
                using BodyCallback = std::function<int(ByteCursor body, uint64_t rangeStart)>;

                /**
                 * Like BodyCallback, but the chunk is delivered with a borrowed
                 * S3BufferTicket. The receiver may call ticket.Acquire() to extend
                 * the buffer's lifetime past the callback return for zero-copy
                 * consumption.
                 * @param body the chunk of object bytes for this delivery.
                 * @param rangeStart byte offset within the object that this chunk starts at.
                 * @param ticket borrowed handle to the CRT-owned buffer holding body.
                 * @return AWS_OP_SUCCESS to continue, or an error code to abort the meta request.
                 */
                using BodyCallbackEx = std::function<int(ByteCursor body, uint64_t rangeStart, S3BufferTicket &ticket)>;

                /**
                 * Invoked once when response headers are available.
                 * @param headers materialized snapshot of the response headers.
                 * @param responseStatus HTTP status code from the response.
                 * @return AWS_OP_SUCCESS to continue, or an error code to abort the meta request.
                 */
                using HeadersCallback = std::function<int(const Vector<Http::HttpHeader> &headers, int responseStatus)>;

                /**
                 * Invoked periodically as bytes flow.
                 * @param bytesTransferred number of bytes since the last invocation.
                 * @param contentLength total length of the transfer in bytes, if known.
                 */
                using ProgressCallback = std::function<void(uint64_t bytesTransferred, uint64_t contentLength)>;

                /**
                 * Invoked once when the meta request terminates (success or failure).
                 * @param result final state of the meta request, including error
                 *        code, HTTP status, and (on failure) the S3 error response
                 *        headers and body. The error response body is a borrowed
                 *        cursor into CRT-owned memory, valid only for the duration
                 *        of this callback; copy it out if you need it.
                 */
                using FinishCallback = std::function<void(const S3MetaRequestResult &result)>;

                /**
                 * Invoked after the CRT has fully torn down the meta request and
                 * will not invoke any further callbacks. The right place to free
                 * any heap state that the callbacks depended on.
                 */
                using ShutdownCallback = std::function<void()>;

                S3MetaRequestOptions(const S3MetaRequestOptions &) = delete;
                S3MetaRequestOptions(S3MetaRequestOptions &&) = delete;
                S3MetaRequestOptions &operator=(const S3MetaRequestOptions &) = delete;
                S3MetaRequestOptions &operator=(S3MetaRequestOptions &&) = delete;

                virtual ~S3MetaRequestOptions() noexcept;

                /**
                 * Override the client-level signing config for this meta request.
                 *
                 * Only the underlying handle is borrowed; no copy is taken
                 * (AwsSigningConfig is non-copyable). The supplied config must
                 * outlive the call that consumes it - S3Client::MakeMetaRequest,
                 * which deep-copies the signing config internally.
                 *
                 * @param config the SigV4 signing config to use for this request.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &SetSigningConfig(const Auth::AwsSigningConfig &config) noexcept;

                /**
                 * Set the checksum configuration for this meta request.
                 *
                 * @param config the checksum configuration.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &SetChecksumConfig(const S3ChecksumConfig &config) noexcept;

                /**
                 * Set a per-request endpoint override. Overrides the scheme and port
                 * the meta request would otherwise derive from the HTTP request. If
                 * the HTTP request already carries a Host header it must match this
                 * endpoint's authority, otherwise MakeMetaRequest fails. The options
                 * object owns a parsed copy of the URI for its lifetime. An invalid
                 * endpoint URI is recorded and causes MakeMetaRequest to fail with
                 * the parse error, rather than being silently ignored.
                 *
                 * @param endpoint the endpoint URI to use for this meta request.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &SetEndpoint(const Io::Uri &endpoint) noexcept;

                /**
                 * Install the response-headers callback.
                 *
                 * @param cb the callback to invoke when response headers arrive.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &SetHeadersCallback(HeadersCallback cb) noexcept;

                /**
                 * Install the progress callback.
                 *
                 * @param cb the callback to invoke as bytes flow.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &SetProgressCallback(ProgressCallback cb) noexcept;

                /**
                 * Install the finish callback. Invoked once when the meta request
                 * terminates.
                 *
                 * @param cb the callback to invoke on completion or failure.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &SetFinishCallback(FinishCallback cb) noexcept;

                /**
                 * Install the shutdown callback. Invoked after the CRT has fully
                 * torn down the meta request; the safe point to free callback
                 * state.
                 *
                 * @param cb the callback to invoke after final teardown.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &SetShutdownCallback(ShutdownCallback cb) noexcept;

                /**
                 * Choose how the destination file supplied to a receive-to-file
                 * factory is opened. Defaults to CreateOrReplace. Ignored when
                 * the meta request has no receive filepath (i.e. a callback sink).
                 *
                 * @param mode the file-open policy.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &SetRecvFileMode(S3RecvFileMode mode) noexcept;

                /**
                 * Set the byte offset the CRT writes at when the recv file
                 * mode is WriteToPosition. Ignored otherwise.
                 *
                 * @param position byte offset within the file.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &SetRecvFilePosition(uint64_t position) noexcept;

                /**
                 * Delete the receive file if the meta request fails. Off by
                 * default; the file is left as-is on failure. Useful when the
                 * caller wants the CRT to clean up its own temp file rather
                 * than doing the delete after the finish callback.
                 *
                 * @param deleteOnFailure whether to delete the file on failure.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &SetRecvFileDeleteOnFailure(bool deleteOnFailure) noexcept;

                /**
                 * Hint the size of the object being uploaded or downloaded.
                 * Used by the CRT to pick a strategy and validate part counts
                 * without an extra HeadObject roundtrip. Pass 0 to clear.
                 *
                 * @param bytes known object size in bytes; 0 to clear.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &SetObjectSizeHint(uint64_t bytes) noexcept;

                /**
                 * Override the client-level target part size for just this
                 * meta request. Same semantics as S3ClientConfig::SetPartSize.
                 * 0 means inherit from the client.
                 *
                 * @param bytes target part size in bytes.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &SetPartSize(uint64_t bytes) noexcept;

                /**
                 * Override the client-level multipart-upload threshold for just
                 * this meta request. Same semantics as
                 * S3ClientConfig::SetMultipartUploadThreshold. 0 means inherit
                 * from the client.
                 *
                 * @param bytes threshold in bytes.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &SetMultipartUploadThreshold(uint64_t bytes) noexcept;

                /// @private
                /// Raw handle for the C layer (aws_s3_client_make_meta_request); not
                /// part of the public API.
                struct aws_s3_meta_request_options *GetUnderlyingHandle() const noexcept;

                /** @return the installed body callback, or an empty function if unset. */
                const BodyCallback &GetBodyCallback() const noexcept { return m_bodyCb; }
                /** @return the installed zero-copy body callback, or an empty function if unset. */
                const BodyCallbackEx &GetBodyCallbackEx() const noexcept { return m_bodyCbEx; }
                /** @return the installed response-headers callback, or an empty function if unset. */
                const HeadersCallback &GetHeadersCallback() const noexcept { return m_headersCb; }
                /** @return the installed progress callback, or an empty function if unset. */
                const ProgressCallback &GetProgressCallback() const noexcept { return m_progressCb; }
                /** @return the installed finish callback, or an empty function if unset. */
                const FinishCallback &GetFinishCallback() const noexcept { return m_finishCb; }
                /** @return the installed shutdown callback, or an empty function if unset. */
                const ShutdownCallback &GetShutdownCallback() const noexcept { return m_shutdownCb; }

                /**
                 * @return a validation error recorded by a Set* setter (ex. an
                 *         invalid endpoint URI), or AWS_ERROR_SUCCESS if none. Checked
                 *         by MakeMetaRequest before the request is issued.
                 */
                int GetLastError() const noexcept { return m_lastError; }

              protected:
                /**
                 * Protected ctor. Only invocable by subclasses. Allocates the C
                 * options struct, sets its type and message, and retains a
                 * shared reference to the HTTP request so the underlying
                 * aws_http_message stays alive for the meta request's lifetime.
                 *
                 * @param type the operation the CRT should orchestrate.
                 * @param request the prepared HTTP request.
                 */
                S3MetaRequestOptions(
                    S3MetaRequestType type,
                    const std::shared_ptr<Http::HttpRequest> &request) noexcept;

                // The CRT C-struct storage (aws_s3_meta_request_options plus the
                // checksum-config backing) lives by value inside Impl, defined in
                // S3.cpp. Keeps the C headers out of this public header and
                // collapses two heap allocations into the single Impl allocation.
                // Protected so subclass ctors can reach it (through m_impl) to
                // populate shape-specific fields directly rather than via a setter.
                struct Impl;
                ScopedResource<Impl> m_impl;
                std::shared_ptr<Http::HttpRequest> m_httpRequest;
                String m_operationName;
                String m_sendFilepath;
                String m_recvFilepath;
                // Endpoint override parsed in place (no heap). aws_uri's internal
                // cursors point into its own buffer, so it must never be moved
                // after parsing - hence a value member parsed directly, not a
                // relocatable wrapper. m_endpointInit tracks whether it holds a
                // parsed URI needing cleanup; m_options->endpoint being non-null
                // is the "set" signal.
                aws_uri m_endpoint;
                bool m_endpointInit;
                // Backs the borrowed pointer the CRT holds via object_size_hint.
                Optional<uint64_t> m_objectSizeHint;
                BodyCallback m_bodyCb;
                BodyCallbackEx m_bodyCbEx;
                HeadersCallback m_headersCb;
                ProgressCallback m_progressCb;
                FinishCallback m_finishCb;
                ShutdownCallback m_shutdownCb;
                // Sticky validation error set by a Set* setter, surfaced at
                // MakeMetaRequest (mirrors the MqttClient builder's LastError()).
                int m_lastError = AWS_ERROR_SUCCESS;
            };

            /**
             * Options for a GetObject meta request. Every download must land
             * somewhere; the three Create overloads pin the delivery path at
             * construction and make it impossible to submit a GetObject that
             * silently drops the body. Which sink is used is determined by
             * the argument type: a BodyCallback, a BodyCallbackEx, or a
             * destination file path.
             */
            class AWS_CRT_CPP_API S3GetObjectMetaRequestOptions final : public S3MetaRequestOptions
            {
              public:
                S3GetObjectMetaRequestOptions(const S3GetObjectMetaRequestOptions &) = delete;
                S3GetObjectMetaRequestOptions(S3GetObjectMetaRequestOptions &&) = delete;
                S3GetObjectMetaRequestOptions &operator=(const S3GetObjectMetaRequestOptions &) = delete;
                S3GetObjectMetaRequestOptions &operator=(S3GetObjectMetaRequestOptions &&) = delete;

                /**
                 * Build options that deliver body chunks through a caller-owned
                 * BodyCallback.
                 *
                 * @param request the prepared HTTP request.
                 * @param cb the body callback to invoke for each chunk.
                 * @return a unique_ptr to the base type, or nullptr on failure.
                 */
                static ScopedResource<S3MetaRequestOptions> Create(
                    const std::shared_ptr<Http::HttpRequest> &request,
                    BodyCallback cb) noexcept;

                /**
                 * Build options that deliver body chunks through a caller-owned
                 * zero-copy BodyCallbackEx, which receives a borrowed
                 * S3BufferTicket for each chunk.
                 *
                 * @param request the prepared HTTP request.
                 * @param cb the zero-copy body callback.
                 * @return a unique_ptr to the base type, or nullptr on failure.
                 */
                static ScopedResource<S3MetaRequestOptions> Create(
                    const std::shared_ptr<Http::HttpRequest> &request,
                    BodyCallbackEx cb) noexcept;

                /**
                 * Build options that stream the response body directly to a
                 * file on disk. Neither BodyCallback nor BodyCallbackEx fires
                 * when this overload is used.
                 *
                 * @param request the prepared HTTP request.
                 * @param recvFilepath destination file path.
                 * @return a unique_ptr to the base type, or nullptr on failure.
                 */
                static ScopedResource<S3MetaRequestOptions> Create(
                    const std::shared_ptr<Http::HttpRequest> &request,
                    const Crt::String &recvFilepath) noexcept;

                /// @private Prefer the Create factories; direct construction
                /// leaves the body sink unset and produces an incomplete object.
                explicit S3GetObjectMetaRequestOptions(const std::shared_ptr<Http::HttpRequest> &request) noexcept;
            };

            /**
             * Options for a PutObject meta request. The body source is pinned
             * at construction: either the HTTP request already carries a body
             * stream (via HttpRequest::SetBody), or a source file path is
             * supplied. aws-c-s3 rejects a PUT with no source. Which source
             * is used is determined by whether a source file path is passed.
             */
            class AWS_CRT_CPP_API S3PutObjectMetaRequestOptions final : public S3MetaRequestOptions
            {
              public:
                S3PutObjectMetaRequestOptions(const S3PutObjectMetaRequestOptions &) = delete;
                S3PutObjectMetaRequestOptions(S3PutObjectMetaRequestOptions &&) = delete;
                S3PutObjectMetaRequestOptions &operator=(const S3PutObjectMetaRequestOptions &) = delete;
                S3PutObjectMetaRequestOptions &operator=(S3PutObjectMetaRequestOptions &&) = delete;

                /**
                 * Build options for a PUT whose body is already attached to the
                 * HTTP request (via HttpRequest::SetBody).
                 *
                 * @param request the prepared HTTP request with body attached.
                 * @return a unique_ptr to the base type, or nullptr on failure.
                 */
                static ScopedResource<S3MetaRequestOptions> Create(
                    const std::shared_ptr<Http::HttpRequest> &request) noexcept;

                /**
                 * Build options for a PUT whose body is read from a file on disk.
                 * Do NOT also attach a body to the HTTP request - aws-c-s3
                 * rejects requests with more than one body source.
                 *
                 * @param request the prepared HTTP request (no body attached).
                 * @param sendFilepath source file path.
                 * @return a unique_ptr to the base type, or nullptr on failure.
                 */
                static ScopedResource<S3MetaRequestOptions> Create(
                    const std::shared_ptr<Http::HttpRequest> &request,
                    const Crt::String &sendFilepath) noexcept;

                /**
                 * Build options for a PUT whose body is supplied incrementally via
                 * S3MetaRequest::Write. Do NOT attach a body to the HTTP request or
                 * supply a send filepath - aws-c-s3 rejects more than one body
                 * source. The object is uploaded as multipart and the content length
                 * need not be known up front.
                 *
                 * @param request the prepared HTTP request (no body attached).
                 * @return a unique_ptr to the base type, or nullptr on failure.
                 */
                static ScopedResource<S3MetaRequestOptions> CreateWithAsyncWrites(
                    const std::shared_ptr<Http::HttpRequest> &request) noexcept;

                /// @private Prefer the Create factories; direct construction
                /// leaves the send filepath unset.
                explicit S3PutObjectMetaRequestOptions(const std::shared_ptr<Http::HttpRequest> &request) noexcept;
            };

            /**
             * Options for a CopyObject meta request. The source is identified
             * by the x-amz-copy-source header on the HTTP request; there is no
             * caller-side body source or sink.
             */
            class AWS_CRT_CPP_API S3CopyObjectMetaRequestOptions final : public S3MetaRequestOptions
            {
              public:
                S3CopyObjectMetaRequestOptions(const S3CopyObjectMetaRequestOptions &) = delete;
                S3CopyObjectMetaRequestOptions(S3CopyObjectMetaRequestOptions &&) = delete;
                S3CopyObjectMetaRequestOptions &operator=(const S3CopyObjectMetaRequestOptions &) = delete;
                S3CopyObjectMetaRequestOptions &operator=(S3CopyObjectMetaRequestOptions &&) = delete;

                /**
                 * Build options for a CopyObject meta request.
                 *
                 * @param request the prepared HTTP request. Must carry the
                 *        x-amz-copy-source header identifying the source object.
                 * @return a unique_ptr to the base type, or nullptr on failure.
                 */
                static ScopedResource<S3MetaRequestOptions> Create(
                    const std::shared_ptr<Http::HttpRequest> &request) noexcept;

                /// @private Prefer the Create factory.
                explicit S3CopyObjectMetaRequestOptions(const std::shared_ptr<Http::HttpRequest> &request) noexcept;
            };

            /**
             * Options for a S3MetaRequestType::Default meta request. Use this
             * for any S3 operation that is not GetObject, PutObject, or
             * CopyObject (ex. CreateBucket, HeadObject, ListObjectsV2). The
             * operation name is required and must be the canonical S3 API
             * operation name; mis-naming can produce incorrect behavior or
             * leak sensitive data on error paths (aws-c-s3 uses the name to
             * drive operation-specific response handling).
             */
            class AWS_CRT_CPP_API S3DefaultObjectMetaRequestOptions final : public S3MetaRequestOptions
            {
              public:
                S3DefaultObjectMetaRequestOptions(const S3DefaultObjectMetaRequestOptions &) = delete;
                S3DefaultObjectMetaRequestOptions(S3DefaultObjectMetaRequestOptions &&) = delete;
                S3DefaultObjectMetaRequestOptions &operator=(const S3DefaultObjectMetaRequestOptions &) = delete;
                S3DefaultObjectMetaRequestOptions &operator=(S3DefaultObjectMetaRequestOptions &&) = delete;

                /**
                 * Build options for a S3MetaRequestType::Default meta request.
                 *
                 * @param request the prepared HTTP request.
                 * @param operationName the S3 API operation name
                 *        (ex. "CreateBucket", "HeadObject", "ListObjectsV2").
                 * @return a unique_ptr to the base type, or nullptr on failure.
                 */
                static ScopedResource<S3MetaRequestOptions> Create(
                    const std::shared_ptr<Http::HttpRequest> &request,
                    const Crt::String &operationName) noexcept;

                /// @private Prefer the Create factory.
                S3DefaultObjectMetaRequestOptions(
                    const std::shared_ptr<Http::HttpRequest> &request,
                    const Crt::String &operationName) noexcept;
            };

            /**
             * High-throughput client for issuing S3 meta requests. Holds the
             * underlying aws_s3_client, including its thread pool, connection
             * pool, and signing config; the C handle is released when the wrapper
             * is destroyed. A single client may serve many concurrent meta
             * requests; create one per region per process unless you have a
             * specific reason to do otherwise.
             */
            class AWS_CRT_CPP_API S3Client final
            {
              public:
                S3Client(const S3Client &) = delete;
                S3Client(S3Client &&) = delete;
                S3Client &operator=(const S3Client &) = delete;
                S3Client &operator=(S3Client &&) = delete;

                explicit S3Client(const S3ClientConfig &config) noexcept;
                ~S3Client() noexcept = default;

                /**
                 * Submit a meta request. The callbacks installed on the options
                 * object are copied into a CRT-owned callback bundle that backs
                 * the running request, so the caller may discard the options
                 * object after this call returns.
                 *
                 * @param options the configured options for this meta request,
                 *        produced by one of the subclass factories. Borrowed for
                 *        the duration of the call only; the caller retains
                 *        ownership.
                 * @return a handle to the running meta request, or nullptr on
                 *         failure. On failure, LastError() returns the CRT error
                 *         code.
                 */
                std::shared_ptr<S3MetaRequest> MakeMetaRequest(S3MetaRequestOptions &options) noexcept;

                /**
                 * Populate an existing signing config with defaults appropriate
                 * for S3, sourced from the given region and credentials provider.
                 * Fills the caller's config in place rather than returning one -
                 * AwsSigningConfig (via ISigningConfig) deletes its copy and move
                 * constructors, so it cannot be returned by value, and this avoids
                 * a heap allocation for the common case where the caller already
                 * owns the config (ex. as a member or on the stack).
                 *
                 * @param config the signing config to populate.
                 * @param region the AWS region to sign requests for.
                 * @param provider the credentials provider used during signing.
                 * @return true on success, or false if provider is null.
                 */
                static bool MakeDefaultSigningConfig(
                    Auth::AwsSigningConfig &config,
                    const String &region,
                    const std::shared_ptr<Auth::ICredentialsProvider> &provider) noexcept;

                /**
                 * @return true if the underlying client was constructed successfully.
                 */
                explicit operator bool() const noexcept { return m_client != nullptr; }

                /**
                 * @return the CRT error code from the most recent failed operation
                 *         on this client, or AWS_ERROR_UNKNOWN if none has been
                 *         recorded.
                 */
                int LastError() const noexcept;

              private:
                ScopedResource<struct aws_s3_client> m_client;
                int m_lastError;
            };

            /**
             * Handle to a single in-flight or recently-completed meta request,
             * obtained from S3Client::MakeMetaRequest. Callbacks continue to
             * fire correctly even if the caller drops their shared_ptr: the
             * CRT's callback bundle keeps its own reference until shutdown.
             */
            class AWS_CRT_CPP_API S3MetaRequest final
            {
              public:
                S3MetaRequest(const S3MetaRequest &) = delete;
                S3MetaRequest(S3MetaRequest &&) = delete;
                S3MetaRequest &operator=(const S3MetaRequest &) = delete;
                S3MetaRequest &operator=(S3MetaRequest &&) = delete;

                ~S3MetaRequest() noexcept = default;

                /**
                 * Request cancellation of the in-flight meta request. The CRT
                 * cancels asynchronously; the finish and shutdown callbacks will
                 * still fire to signal final teardown.
                 */
                void Cancel() noexcept;

                /**
                 * Move the flow-control read window forward by the given number of
                 * bytes. Only meaningful when the client was configured with read
                 * backpressure (S3ClientConfig::SetReadBackpressure): the CRT pauses
                 * delivering body data once the window is exhausted, and the
                 * application must call this to let more data flow. A typical pattern
                 * is to increment by the size of each chunk once it has been consumed.
                 *
                 * @param bytes number of bytes to add to the read window.
                 */
                void IncrementReadWindow(uint64_t bytes) noexcept;

                /**
                 * Write the next chunk of body data for an async-writes PUT (see
                 * S3PutObjectMetaRequestOptions::CreateWithAsyncWrites). The returned
                 * future completes with the CRT error code (0 on success) once the CRT
                 * is ready to accept more data. You MUST NOT call Write again until the
                 * previous call's future has completed. Pass eof=true for the final
                 * chunk and do not call Write again afterward.
                 *
                 * @param data the chunk of body bytes to send; may be any size.
                 * @param eof true if this is the final chunk.
                 * @return a future resolving to the CRT error code (0 on success).
                 */
                std::future<int> Write(ByteCursor data, bool eof) noexcept;

                /**
                 * @return the CRT error code from the most recent failed operation
                 *         on this meta request, or AWS_ERROR_UNKNOWN if none has
                 *         been recorded.
                 */
                int LastError() const noexcept;

                /// @private
                /// Callers must call SetUnderlyingHandle before publishing the
                /// wrapper.
                S3MetaRequest() noexcept;

                /// @private
                /// Takes ownership of the C handle returned by
                /// aws_s3_client_make_meta_request (or nullptr on failure).
                void SetUnderlyingHandle(struct aws_s3_meta_request *handle) noexcept;

                /// @private
                void SetLastError(int errorCode) noexcept { m_lastError = errorCode; }

              private:
                ScopedResource<struct aws_s3_meta_request> m_metaRequest;
                int m_lastError;
            };

        } // namespace S3
    } // namespace Crt
} // namespace Aws
