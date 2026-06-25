#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/s3/s3_client.h>

#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>
#include <aws/crt/auth/Sigv4Signing.h>
#include <aws/crt/http/HttpRequestResponse.h>

#include <cstdint>
#include <functional>
#include <memory>

struct aws_s3_buffer_ticket;
struct aws_http_headers;
struct aws_byte_buf;

namespace Aws
{
    namespace Crt
    {
        namespace Auth
        {
            class ICredentialsProvider;
        }

        namespace Io
        {
            class ClientBootstrap;
        }

        namespace S3
        {
            /**
             * The kind of S3 operation that a meta request will perform.
             * Default is a single-request pass-through; the other values enable the
             * CRT's multi-part orchestration for the named operation.
             */
            enum class S3MetaRequestType
            {
                Default = AWS_S3_META_REQUEST_TYPE_DEFAULT,
                GetObject = AWS_S3_META_REQUEST_TYPE_GET_OBJECT,
                PutObject = AWS_S3_META_REQUEST_TYPE_PUT_OBJECT,
                CopyObject = AWS_S3_META_REQUEST_TYPE_COPY_OBJECT,
            };

            /**
             * Where a request-side checksum should be placed on the wire.
             * Trailer is the recommended choice for streaming uploads; Header is used
             * when the full payload is in hand before the request is sent.
             */
            enum class S3ChecksumLocation
            {
                None = AWS_SCL_NONE,
                Header = AWS_SCL_HEADER,
                Trailer = AWS_SCL_TRAILER,
            };

            /**
             * The checksum algorithm used for request signing or response validation.
             * Only the algorithms supported by aws-c-s3 are surfaced here.
             */
            enum class S3ChecksumAlgorithm
            {
                None = AWS_SCA_NONE,
                Crc32c = AWS_SCA_CRC32C,
                Crc32 = AWS_SCA_CRC32,
                Sha1 = AWS_SCA_SHA1,
                Sha256 = AWS_SCA_SHA256,
                Crc64Nvme = AWS_SCA_CRC64NVME,
            };

            class S3Client;
            class S3MetaRequest;
            class S3MetaRequestOptions;

            /**
             * Result delivered to FinishCallback when a meta request terminates.
             * Mirrors aws_s3_meta_request_result. The header / body pointers point
             * into CRT-owned memory and are valid only for the duration of the
             * FinishCallback invocation; copy out any fields you need to retain.
             */
            struct AWS_CRT_CPP_API S3MetaRequestResult
            {
                /** AWS_ERROR_SUCCESS on success, a CRT error code otherwise. */
                int errorCode;

                /** HTTP status code of the failed request, or 0 if not applicable. */
                int responseStatus;

                /** Headers of the S3 error response, or nullptr if not applicable. */
                const struct aws_http_headers *errorResponseHeaders;

                /** Body of the S3 error response, or nullptr if not applicable. */
                const struct aws_byte_buf *errorResponseBody;

                /** True if the server-side checksum was validated against a calculated value. */
                bool didValidateChecksum;

                /** Algorithm used for checksum validation, when didValidateChecksum is true. */
                S3ChecksumAlgorithm validationAlgorithm;
            };

            /**
             * Owning handle to a buffer that the CRT has loaned out as part of a
             * stream download. The CRT delivers each received part by invoking the
             * BodyCallbackEx with a borrowed ticket pointing at memory in the CRT's
             * pool. The ticket is alive only for the duration of that callback; call
             * Acquire() inside the callback to obtain a new owning wrapper that
             * keeps the buffer valid until the new wrapper is destroyed.
             */
            class AWS_CRT_CPP_API S3BufferTicket final
            {
              public:
                S3BufferTicket(const S3BufferTicket &) = delete;
                S3BufferTicket(S3BufferTicket &&) = delete;
                S3BufferTicket &operator=(const S3BufferTicket &) = delete;
                S3BufferTicket &operator=(S3BufferTicket &&) = delete;

                ~S3BufferTicket() noexcept;

                /**
                 * Bump the underlying refcount and produce a new wrapper that owns
                 * the new reference. The returned wrapper releases its reference
                 * when it is destroyed.
                 *
                 * @return a new wrapper holding an additional reference to the same buffer.
                 */
                std::unique_ptr<S3BufferTicket> Acquire() noexcept;

                // Public to allow callback trampolines defined in S3.cpp to wrap
                // a borrowed aws_s3_buffer_ticket; not intended for customer use.
                explicit S3BufferTicket(struct aws_s3_buffer_ticket *ticket) noexcept;

              private:
                struct aws_s3_buffer_ticket *m_ticket;
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
                S3ChecksumConfig &WithLocation(S3ChecksumLocation location) noexcept
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
                S3ChecksumConfig &WithChecksumAlgorithm(S3ChecksumAlgorithm algorithm) noexcept
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
                S3ChecksumConfig &WithValidateResponseChecksum(bool validate) noexcept
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
             * Configuration object used to construct an S3Client. Build one with the
             * fluent With* setters, then hand it to the S3Client constructor. The
             * object owns the backing storage for any string-valued fields, so it
             * must outlive the construction call (its dtor frees the storage).
             */
            class AWS_CRT_CPP_API S3ClientConfig final
            {
              public:
                S3ClientConfig(const S3ClientConfig &) = delete;
                S3ClientConfig(S3ClientConfig &&) = delete;
                S3ClientConfig &operator=(const S3ClientConfig &) = delete;
                S3ClientConfig &operator=(S3ClientConfig &&) = delete;

                S3ClientConfig(Allocator *allocator = ApiAllocator()) noexcept;
                ~S3ClientConfig() noexcept;

                /**
                 * Set the AWS region the client will sign requests for.
                 *
                 * @param region the region string, e.g. "us-east-1".
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &WithRegion(const Crt::String &region) noexcept;

                /**
                 * Set the target aggregate throughput the client will tune toward,
                 * in gigabits per second. This drives the CRT's pool sizing.
                 *
                 * @param gbps target throughput in Gbps.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &WithThroughputTargetGbps(double gbps) noexcept;

                /**
                 * Set the target part size for multipart transfers. The CRT may
                 * increase the effective part size for individual transfers if the
                 * target would require more than 10,000 parts.
                 *
                 * @param bytes target part size in bytes.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &WithPartSize(uint64_t bytes) noexcept;

                /**
                 * Set the size threshold above which an upload is split into a
                 * multipart upload.
                 *
                 * @param bytes threshold in bytes.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &WithMultipartUploadThreshold(uint64_t bytes) noexcept;

                /**
                 * Set the upper bound on memory the CRT may buffer for in-flight
                 * parts across all meta requests created with this client.
                 *
                 * @param bytes memory ceiling in bytes.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &WithMemoryLimit(uint64_t bytes) noexcept;

                /**
                 * Set the default signing config used for requests this client
                 * issues. Individual meta requests may override this via
                 * S3MetaRequestOptions::WithSigningConfig.
                 *
                 * @param config the SigV4 signing config.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &WithSigningConfig(const Auth::AwsSigningConfig &config) noexcept;

                /**
                 * Set the client bootstrap used for connection establishment. If
                 * not set, the process-global default bootstrap is used (see
                 * Aws::Crt::ApiHandle::GetOrCreateStaticDefaultClientBootstrap).
                 *
                 * @param bootstrap the client bootstrap.
                 * @return this object, to allow chaining.
                 */
                S3ClientConfig &WithClientBootstrap(Io::ClientBootstrap &bootstrap) noexcept;

              private:
                friend class S3Client;
                struct aws_s3_client_config *m_config;
                Crt::String m_region;
                Allocator *m_allocator;
            };

            /**
             * Configuration object used to launch a single meta request. Build one
             * with the fluent With* setters, then hand it to
             * S3Client::MakeMetaRequest. The callbacks installed here are moved
             * onto the resulting S3MetaRequest so they remain valid for the meta
             * request's full lifetime, which extends past the destruction of this
             * options object.
             *
             * Note: BodyCallback and BodyCallbackEx are mutually exclusive. Set one
             * or the other; setting both causes MakeMetaRequest to fail.
             * BodyCallback is the simple variant; BodyCallbackEx exposes the CRT
             * buffer ticket so the receiver can implement zero-copy stream
             * downloads.
             */
            class AWS_CRT_CPP_API S3MetaRequestOptions final
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
                 * @param headers borrowed handle to the response header set; valid
                 *        only for the duration of this callback. Copy out any
                 *        fields you need to retain.
                 * @param responseStatus HTTP status code from the response.
                 * @return AWS_OP_SUCCESS to continue, or an error code to abort the meta request.
                 */
                using HeadersCallback = std::function<int(const struct aws_http_headers *headers, int responseStatus)>;

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
                 *        headers and body. Borrowed fields are valid only for the
                 *        duration of this callback.
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

                S3MetaRequestOptions(Allocator *allocator = ApiAllocator()) noexcept;
                ~S3MetaRequestOptions() noexcept;

                /**
                 * Set the meta request type (PutObject, GetObject, CopyObject, or Default).
                 *
                 * @param type the operation the CRT should orchestrate.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &WithType(S3MetaRequestType type) noexcept;

                /**
                 * Override the client-level signing config for this meta request.
                 *
                 * @param config the SigV4 signing config to use for this request.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &WithSigningConfig(const Auth::AwsSigningConfig &config) noexcept;

                /**
                 * Set the HTTP request that describes the operation. The options
                 * object retains a shared reference to keep the underlying
                 * aws_http_message alive for the meta request's lifetime.
                 *
                 * @param request the prepared HTTP request.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &WithHttpRequest(const std::shared_ptr<Http::HttpRequest> &request) noexcept;

                /**
                 * Configure a file-based upload. The CRT reads directly from the
                 * named path; do not also set a body on the HTTP request.
                 *
                 * @param path source file path.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &WithSendFilepath(const Crt::String &path) noexcept;

                /**
                 * Configure a file-based download. The CRT writes directly to the
                 * named path; BodyCallback is not invoked when this is set.
                 *
                 * @param path destination file path.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &WithRecvFilepath(const Crt::String &path) noexcept;

                /**
                 * Set the checksum configuration for this meta request.
                 *
                 * @param config the checksum configuration.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &WithChecksumConfig(const S3ChecksumConfig &config) noexcept;

                /**
                 * Install the body-delivery callback for downloads. Mutually
                 * exclusive with WithBodyCallbackEx.
                 *
                 * @param cb the callback to invoke for each delivered chunk.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &WithBodyCallback(BodyCallback cb) noexcept;

                /**
                 * Install the zero-copy body-delivery callback for downloads. The
                 * callback receives a borrowed S3BufferTicket. Mutually exclusive
                 * with WithBodyCallback.
                 *
                 * @param cb the zero-copy body callback.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &WithBodyCallbackEx(BodyCallbackEx cb) noexcept;

                /**
                 * Install the response-headers callback.
                 *
                 * @param cb the callback to invoke when response headers arrive.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &WithHeadersCallback(HeadersCallback cb) noexcept;

                /**
                 * Install the progress callback.
                 *
                 * @param cb the callback to invoke as bytes flow.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &WithProgressCallback(ProgressCallback cb) noexcept;

                /**
                 * Install the finish callback. Invoked once when the meta request
                 * terminates.
                 *
                 * @param cb the callback to invoke on completion or failure.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &WithFinishCallback(FinishCallback cb) noexcept;

                /**
                 * Install the shutdown callback. Invoked after the CRT has fully
                 * torn down the meta request; the safe point to free callback
                 * state.
                 *
                 * @param cb the callback to invoke after final teardown.
                 * @return this object, to allow chaining.
                 */
                S3MetaRequestOptions &WithShutdownCallback(ShutdownCallback cb) noexcept;

              private:
                friend class S3Client;
                struct aws_s3_meta_request_options *m_options;
                struct aws_s3_checksum_config *m_checksumConfig;
                Allocator *m_allocator;
                std::shared_ptr<Http::HttpRequest> m_httpRequest;
                Crt::String m_sendFilepath;
                Crt::String m_recvFilepath;
                BodyCallback m_bodyCb;
                BodyCallbackEx m_bodyCbEx;
                HeadersCallback m_headersCb;
                ProgressCallback m_progressCb;
                FinishCallback m_finishCb;
                ShutdownCallback m_shutdownCb;
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

                S3Client(const S3ClientConfig &config, Allocator *allocator = ApiAllocator()) noexcept;
                ~S3Client() noexcept;

                /**
                 * Submit a meta request. The callbacks installed on the options
                 * object are moved onto the returned S3MetaRequest, so the caller
                 * may discard the options object after this call returns.
                 *
                 * @param options the configured options for this meta request.
                 * @return a handle to the running meta request, or nullptr on
                 *         failure. On failure, LastError() returns the CRT error
                 *         code.
                 */
                std::shared_ptr<S3MetaRequest> MakeMetaRequest(S3MetaRequestOptions &options) noexcept;

                /**
                 * Build a SigV4 signing config with defaults appropriate for S3,
                 * populated from the given region and credentials provider.
                 * Returned via shared_ptr because AwsSigningConfig (via ISigningConfig)
                 * deletes its copy and move constructors and cannot be returned by value.
                 *
                 * @param region the AWS region to sign requests for.
                 * @param provider the credentials provider used during signing.
                 * @param allocator allocator used for the returned config.
                 * @return a shared_ptr to a fully-initialized AwsSigningConfig, or nullptr on failure.
                 */
                static std::shared_ptr<Auth::AwsSigningConfig> MakeDefaultSigningConfig(
                    const Crt::String &region,
                    const std::shared_ptr<Auth::ICredentialsProvider> &provider,
                    Allocator *allocator = ApiAllocator()) noexcept;

                /**
                 * @return true if the underlying client was constructed successfully.
                 */
                explicit operator bool() const noexcept { return m_client != nullptr; }

                /**
                 * @return the CRT error code from the most recent failed operation
                 *         on this client, or AWS_ERROR_UNKNOWN if none has been
                 *         recorded.
                 */
                int LastError() const noexcept { return m_lastError ? m_lastError : AWS_ERROR_UNKNOWN; }

              private:
                struct aws_s3_client *m_client;
                int m_lastError;
            };

            /**
             * Handle to a single in-flight or recently-completed meta request.
             * Obtained from S3Client::MakeMetaRequest. The handle's lifetime is
             * independent of the C-side meta request: the CRT holds its own
             * reference (via a heap-allocated callback bundle in S3.cpp) for the
             * duration of the request, so callbacks continue to fire correctly
             * even if the caller drops their shared_ptr.
             */
            class AWS_CRT_CPP_API S3MetaRequest final
            {
              public:
                S3MetaRequest(const S3MetaRequest &) = delete;
                S3MetaRequest(S3MetaRequest &&) = delete;
                S3MetaRequest &operator=(const S3MetaRequest &) = delete;
                S3MetaRequest &operator=(S3MetaRequest &&) = delete;

                ~S3MetaRequest() noexcept;

                /**
                 * Request cancellation of the in-flight meta request. The CRT
                 * cancels asynchronously; the finish and shutdown callbacks will
                 * still fire to signal final teardown.
                 */
                void Cancel() noexcept;

                /**
                 * @return the CRT error code from the most recent failed operation
                 *         on this meta request, or AWS_ERROR_UNKNOWN if none has
                 *         been recorded.
                 */
                int LastError() const noexcept { return m_lastError ? m_lastError : AWS_ERROR_UNKNOWN; }

              private:
                friend class S3Client;
                S3MetaRequest() noexcept;

                struct aws_s3_meta_request *m_metaRequest;
                int m_lastError;
            };

        } // namespace S3
    } // namespace Crt
} // namespace Aws
