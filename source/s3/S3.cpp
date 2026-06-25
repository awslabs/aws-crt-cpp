/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/s3/S3.h>

#include <aws/crt/Api.h>
#include <aws/crt/auth/Credentials.h>
#include <aws/crt/io/Bootstrap.h>

#include <aws/auth/signing_config.h>
#include <aws/s3/s3_buffer_pool.h>
#include <aws/s3/s3_client.h>

namespace Aws
{
    namespace Crt
    {
        namespace S3
        {
            /*****************************************************
             *
             * S3BufferTicket
             *
             *****************************************************/
            S3BufferTicket::S3BufferTicket(struct aws_s3_buffer_ticket *ticket) noexcept : m_ticket(ticket) {}

            S3BufferTicket::~S3BufferTicket() noexcept
            {
                if (m_ticket != nullptr)
                {
                    aws_s3_buffer_ticket_release(m_ticket);
                }
            }

            std::unique_ptr<S3BufferTicket> S3BufferTicket::Acquire() noexcept
            {
                struct aws_s3_buffer_ticket *acquired = aws_s3_buffer_ticket_acquire(m_ticket);
                return std::unique_ptr<S3BufferTicket>(new S3BufferTicket(acquired));
            }

            /*****************************************************
             *
             * S3ChecksumConfig
             *
             *****************************************************/
            S3ChecksumConfig::S3ChecksumConfig() noexcept
                : m_location(S3ChecksumLocation::None), m_algorithm(S3ChecksumAlgorithm::None),
                  m_validateResponseChecksum(false)
            {
            }

            /*****************************************************
             *
             * S3ClientConfig
             *
             *****************************************************/
            S3ClientConfig::S3ClientConfig(Crt::Allocator *allocator) noexcept
                : m_config(static_cast<struct aws_s3_client_config *>(
                      aws_mem_calloc(allocator, 1, sizeof(struct aws_s3_client_config)))),
                  m_allocator(allocator)
            {
            }

            S3ClientConfig::~S3ClientConfig() noexcept
            {
                aws_mem_release(m_allocator, m_config);
            }

            S3ClientConfig &S3ClientConfig::WithRegion(const Crt::String &region) noexcept
            {
                m_region = region;
                m_config->region = ByteCursorFromCString(m_region.c_str());
                return *this;
            }

            S3ClientConfig &S3ClientConfig::WithThroughputTargetGbps(double gbps) noexcept
            {
                m_config->throughput_target_gbps = gbps;
                return *this;
            }

            S3ClientConfig &S3ClientConfig::WithPartSize(uint64_t bytes) noexcept
            {
                m_config->part_size = bytes;
                return *this;
            }

            S3ClientConfig &S3ClientConfig::WithMultipartUploadThreshold(uint64_t bytes) noexcept
            {
                m_config->multipart_upload_threshold = bytes;
                return *this;
            }

            S3ClientConfig &S3ClientConfig::WithMemoryLimit(uint64_t bytes) noexcept
            {
                m_config->memory_limit_in_bytes = bytes;
                return *this;
            }

            S3ClientConfig &S3ClientConfig::WithSigningConfig(const Auth::AwsSigningConfig &config) noexcept
            {
                m_config->signing_config = config.GetUnderlyingHandle();
                return *this;
            }

            S3ClientConfig &S3ClientConfig::WithClientBootstrap(Io::ClientBootstrap &bootstrap) noexcept
            {
                m_config->client_bootstrap = bootstrap.GetUnderlyingHandle();
                return *this;
            }

            /*****************************************************
             *
             * S3MetaRequestOptions
             *
             *****************************************************/
            S3MetaRequestOptions::S3MetaRequestOptions(Crt::Allocator *allocator) noexcept
                : m_options(static_cast<struct aws_s3_meta_request_options *>(
                      aws_mem_calloc(allocator, 1, sizeof(struct aws_s3_meta_request_options)))),
                  m_checksumConfig(nullptr), m_allocator(allocator)
            {
            }

            S3MetaRequestOptions::~S3MetaRequestOptions() noexcept
            {
                if (m_checksumConfig)
                {
                    aws_mem_release(m_allocator, m_checksumConfig);
                }
                aws_mem_release(m_allocator, m_options);
            }

            S3MetaRequestOptions &S3MetaRequestOptions::WithType(S3MetaRequestType type) noexcept
            {
                m_options->type = static_cast<enum aws_s3_meta_request_type>(type);
                return *this;
            }

            S3MetaRequestOptions &S3MetaRequestOptions::WithSigningConfig(const Auth::AwsSigningConfig &config) noexcept
            {
                m_options->signing_config = config.GetUnderlyingHandle();
                return *this;
            }

            S3MetaRequestOptions &S3MetaRequestOptions::WithHttpRequest(
                const std::shared_ptr<Http::HttpRequest> &request) noexcept
            {
                m_httpRequest = request;
                m_options->message = request ? request->GetUnderlyingMessage() : nullptr;
                return *this;
            }

            S3MetaRequestOptions &S3MetaRequestOptions::WithSendFilepath(const Crt::String &path) noexcept
            {
                m_sendFilepath = path;
                m_options->send_filepath = ByteCursorFromCString(m_sendFilepath.c_str());
                return *this;
            }

            S3MetaRequestOptions &S3MetaRequestOptions::WithRecvFilepath(const Crt::String &path) noexcept
            {
                m_recvFilepath = path;
                m_options->recv_filepath = ByteCursorFromCString(m_recvFilepath.c_str());
                return *this;
            }

            S3MetaRequestOptions &S3MetaRequestOptions::WithChecksumConfig(const S3ChecksumConfig &config) noexcept
            {
                if (!m_checksumConfig)
                {
                    m_checksumConfig = static_cast<struct aws_s3_checksum_config *>(
                        aws_mem_calloc(m_allocator, 1, sizeof(struct aws_s3_checksum_config)));
                }
                m_checksumConfig->location = static_cast<enum aws_s3_checksum_location>(config.GetLocation());
                m_checksumConfig->checksum_algorithm =
                    static_cast<enum aws_s3_checksum_algorithm>(config.GetChecksumAlgorithm());
                m_checksumConfig->validate_response_checksum = config.GetValidateResponseChecksum();
                m_options->checksum_config = m_checksumConfig;
                return *this;
            }

            S3MetaRequestOptions &S3MetaRequestOptions::WithBodyCallback(BodyCallback cb) noexcept
            {
                m_bodyCb = std::move(cb);
                return *this;
            }
            S3MetaRequestOptions &S3MetaRequestOptions::WithBodyCallbackEx(BodyCallbackEx cb) noexcept
            {
                m_bodyCbEx = std::move(cb);
                return *this;
            }
            S3MetaRequestOptions &S3MetaRequestOptions::WithHeadersCallback(HeadersCallback cb) noexcept
            {
                m_headersCb = std::move(cb);
                return *this;
            }
            S3MetaRequestOptions &S3MetaRequestOptions::WithProgressCallback(ProgressCallback cb) noexcept
            {
                m_progressCb = std::move(cb);
                return *this;
            }
            S3MetaRequestOptions &S3MetaRequestOptions::WithFinishCallback(FinishCallback cb) noexcept
            {
                m_finishCb = std::move(cb);
                return *this;
            }
            S3MetaRequestOptions &S3MetaRequestOptions::WithShutdownCallback(ShutdownCallback cb) noexcept
            {
                m_shutdownCb = std::move(cb);
                return *this;
            }

            /*****************************************************
             *
             * Callback data and trampolines
             *
             *****************************************************/

            struct S3MetaRequestCallbackData
            {
                explicit S3MetaRequestCallbackData(Allocator *alloc) : allocator(alloc) {}
                Allocator *allocator;
                std::shared_ptr<S3MetaRequest> wrapper;
                S3MetaRequestOptions::BodyCallback bodyCb;
                S3MetaRequestOptions::BodyCallbackEx bodyCbEx;
                S3MetaRequestOptions::HeadersCallback headersCb;
                S3MetaRequestOptions::ProgressCallback progressCb;
                S3MetaRequestOptions::FinishCallback finishCb;
                S3MetaRequestOptions::ShutdownCallback shutdownCb;
            };
            extern "C"
            {
                static int s_onHeaders(
                    struct aws_s3_meta_request * /*meta*/,
                    const struct aws_http_headers *headers,
                    int responseStatus,
                    void *user_data)
                {
                    auto *data = static_cast<S3MetaRequestCallbackData *>(user_data);
                    return data->headersCb ? data->headersCb(headers, responseStatus) : AWS_OP_SUCCESS;
                }

                static int s_onBody(
                    struct aws_s3_meta_request * /*meta*/,
                    const struct aws_byte_cursor *body,
                    uint64_t rangeStart,
                    void *user_data)
                {
                    auto *data = static_cast<S3MetaRequestCallbackData *>(user_data);
                    return data->bodyCb ? data->bodyCb(*body, rangeStart) : AWS_OP_SUCCESS;
                }

                static int s_onBodyEx(
                    struct aws_s3_meta_request * /*meta*/,
                    const struct aws_byte_cursor *body,
                    const struct aws_s3_meta_request_receive_body_extra_info info,
                    void *user_data)
                {
                    auto *data = static_cast<S3MetaRequestCallbackData *>(user_data);
                    if (!data->bodyCbEx)
                    {
                        return AWS_OP_SUCCESS;
                    }
                    // info.ticket may be null when the request was not allocated
                    // from the buffer pool (e.g. HEAD_OBJECT). Fall back to the
                    // non-ticketed callback shape via a nullptr-backed wrapper.
                    if (info.ticket == nullptr)
                    {
                        S3BufferTicket borrowed(nullptr);
                        return data->bodyCbEx(*body, info.range_start, borrowed);
                    }
                    S3BufferTicket borrowed(aws_s3_buffer_ticket_acquire(info.ticket));
                    return data->bodyCbEx(*body, info.range_start, borrowed);
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
                        cppResult.errorResponseHeaders = result->error_response_headers;
                        cppResult.errorResponseBody = result->error_response_body;
                        cppResult.didValidateChecksum = result->did_validate;
                        cppResult.validationAlgorithm =
                            static_cast<S3ChecksumAlgorithm>(result->validation_algorithm);
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
                    Allocator *allocator = data->allocator;
                    Aws::Crt::Delete(data, allocator);
                }
            }

            /*****************************************************
             *
             * S3Client
             *
             *****************************************************/
            S3Client::S3Client(const S3ClientConfig &config, Crt::Allocator *allocator) noexcept
                : m_client(nullptr), m_lastError(AWS_ERROR_SUCCESS)
            {
                // aws-c-s3 mandates a non-null client_bootstrap. If the caller did not
                // set one on the config, fall back to the process-global default
                if (config.m_config->client_bootstrap == nullptr)
                {
                    Io::ClientBootstrap *defaultBootstrap = ApiHandle::GetOrCreateStaticDefaultClientBootstrap();
                    if (defaultBootstrap != nullptr)
                    {
                        config.m_config->client_bootstrap = defaultBootstrap->GetUnderlyingHandle();
                    }
                }

                m_client = aws_s3_client_new(allocator, config.m_config);
                m_lastError = m_client ? AWS_ERROR_SUCCESS : aws_last_error();
            }

            S3Client::~S3Client() noexcept
            {
                aws_s3_client_release(m_client);
            }

            std::shared_ptr<Auth::AwsSigningConfig> S3Client::MakeDefaultSigningConfig(
                const Crt::String &region,
                const std::shared_ptr<Auth::ICredentialsProvider> &provider,
                Crt::Allocator *allocator) noexcept
            {
                auto out = Aws::Crt::MakeShared<Auth::AwsSigningConfig>(allocator, allocator);
                aws_s3_init_default_signing_config(
                    const_cast<aws_signing_config_aws *>(out->GetUnderlyingHandle()),
                    ByteCursorFromCString(region.c_str()),
                    provider->GetUnderlyingHandle());
                return out;
            }

            std::shared_ptr<S3MetaRequest> S3Client::MakeMetaRequest(S3MetaRequestOptions &options) noexcept
            {
                if (options.m_bodyCb && options.m_bodyCbEx)
                {
                    m_lastError = AWS_ERROR_INVALID_ARGUMENT;
                    return nullptr;
                }

                Allocator *allocator = options.m_allocator;
                auto *callbackData = Aws::Crt::New<S3MetaRequestCallbackData>(allocator, allocator);
                callbackData->bodyCb = std::move(options.m_bodyCb);
                callbackData->bodyCbEx = std::move(options.m_bodyCbEx);
                callbackData->headersCb = std::move(options.m_headersCb);
                callbackData->progressCb = std::move(options.m_progressCb);
                callbackData->finishCb = std::move(options.m_finishCb);
                callbackData->shutdownCb = std::move(options.m_shutdownCb);

                auto wrapper = std::shared_ptr<S3MetaRequest>(new S3MetaRequest());
                callbackData->wrapper = wrapper;

                options.m_options->user_data = callbackData;
                options.m_options->headers_callback = s_onHeaders;
                options.m_options->body_callback = callbackData->bodyCb ? s_onBody : nullptr;
                options.m_options->body_callback_ex = callbackData->bodyCbEx ? s_onBodyEx : nullptr;
                options.m_options->progress_callback = s_onProgress;
                options.m_options->finish_callback = s_onFinish;
                options.m_options->shutdown_callback = s_onShutdown;

                wrapper->m_metaRequest = aws_s3_client_make_meta_request(m_client, options.m_options);
                if (!wrapper->m_metaRequest)
                {
                    m_lastError = aws_last_error();
                    Aws::Crt::Delete(callbackData, allocator);
                    return nullptr;
                }
                return wrapper;
            }

            /*****************************************************
             *
             * S3MetaRequest
             *
             *****************************************************/
            S3MetaRequest::S3MetaRequest() noexcept : m_metaRequest(nullptr), m_lastError(AWS_ERROR_SUCCESS) {}

            S3MetaRequest::~S3MetaRequest() noexcept
            {
                aws_s3_meta_request_release(m_metaRequest);
            }

            void S3MetaRequest::Cancel() noexcept
            {
                aws_s3_meta_request_cancel(m_metaRequest);
            }

        } // namespace S3
    } // namespace Crt
} // namespace Aws
