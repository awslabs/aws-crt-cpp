/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/external/cJSON.h>
#include <aws/crt/io/TlsOptions.h>

#include <aws/auth/auth.h>
#include <aws/common/ref_count.h>
#include <aws/http/http.h>
#include <aws/mqtt/mqtt.h>

namespace Aws
{
    namespace Crt
    {
        Allocator *g_allocator = Aws::Crt::DefaultAllocator();

        static Crypto::CreateHashCallback s_BYOCryptoNewMD5Callback;
        static Crypto::CreateHashCallback s_BYOCryptoNewSHA256Callback;
        static Crypto::CreateHMACCallback s_BYOCryptoNewSHA256HMACCallback;
        static Io::NewClientTlsHandlerCallback s_BYOCryptoNewClientTlsHandlerCallback;
        static Io::NewTlsContextImplCallback s_BYOCryptoNewTlsContextImplCallback;
        static Io::DeleteTlsContextImplCallback s_BYOCryptoDeleteTlsContextImplCallback;
        static Io::IsTlsAlpnSupportedCallback s_BYOCryptoIsTlsAlpnSupportedCallback;

        static void *s_cJSONAlloc(size_t sz) { return aws_mem_acquire(g_allocator, sz); }

        static void s_cJSONFree(void *ptr) { return aws_mem_release(g_allocator, ptr); }

        static void s_initApi(Allocator *allocator)
        {
            // sets up the StlAllocator for use.
            g_allocator = allocator;
            aws_http_library_init(allocator);
            aws_mqtt_library_init(allocator);
            aws_auth_library_init(allocator);

            cJSON_Hooks hooks;
            hooks.malloc_fn = s_cJSONAlloc;
            hooks.free_fn = s_cJSONFree;
            cJSON_InitHooks(&hooks);
        }

        ApiHandle::ApiHandle(Allocator *allocator) noexcept
            : logger(), shutdownBehavior(ApiHandleShutdownBehavior::Blocking)
        {
            s_initApi(allocator);
        }

        ApiHandle::ApiHandle() noexcept : logger(), shutdownBehavior(ApiHandleShutdownBehavior::Blocking)
        {
            s_initApi(g_allocator);
        }

        ApiHandle::~ApiHandle()
        {
            if (shutdownBehavior == ApiHandleShutdownBehavior::Blocking)
            {
                aws_thread_join_all_managed();
            }

            if (aws_logger_get() == &logger)
            {
                aws_logger_set(NULL);
                aws_logger_clean_up(&logger);
            }

            g_allocator = nullptr;
            aws_auth_library_clean_up();
            aws_mqtt_library_clean_up();
            aws_http_library_clean_up();

            s_BYOCryptoNewMD5Callback = nullptr;
            s_BYOCryptoNewSHA256Callback = nullptr;
            s_BYOCryptoNewSHA256HMACCallback = nullptr;
            s_BYOCryptoNewClientTlsHandlerCallback = nullptr;
            s_BYOCryptoNewTlsContextImplCallback = nullptr;
            s_BYOCryptoDeleteTlsContextImplCallback = nullptr;
            s_BYOCryptoIsTlsAlpnSupportedCallback = nullptr;
        }

        void ApiHandle::InitializeLogging(Aws::Crt::LogLevel level, const char *filename)
        {
            struct aws_logger_standard_options options;
            AWS_ZERO_STRUCT(options);

            options.level = (enum aws_log_level)level;
            options.filename = filename;

            InitializeLoggingCommon(options);
        }

        void ApiHandle::InitializeLogging(Aws::Crt::LogLevel level, FILE *fp)
        {
            struct aws_logger_standard_options options;
            AWS_ZERO_STRUCT(options);

            options.level = (enum aws_log_level)level;
            options.file = fp;

            InitializeLoggingCommon(options);
        }

        void ApiHandle::InitializeLoggingCommon(struct aws_logger_standard_options &options)
        {
            if (aws_logger_get() == &logger)
            {
                aws_logger_set(NULL);
                aws_logger_clean_up(&logger);
                if (options.level == AWS_LL_NONE)
                {
                    AWS_ZERO_STRUCT(logger);
                    return;
                }
            }

            if (aws_logger_init_standard(&logger, g_allocator, &options))
            {
                return;
            }

            aws_logger_set(&logger);
        }

        void ApiHandle::SetShutdownBehavior(ApiHandleShutdownBehavior behavior) { shutdownBehavior = behavior; }

#ifdef BYO_CRYPTO
        static struct aws_hash *s_MD5New(struct aws_allocator *allocator)
        {
            auto hash = s_BYOCryptoNewMD5Callback(AWS_MD5_LEN, allocator);
            if (hash)
            {
                return hash->SeatForCInterop(hash);
            }
            else
            {
                return nullptr;
            }
        }

        void ApiHandle::SetBYOCryptoNewMD5Callback(Crypto::CreateHashCallback &&callback)
        {
            s_BYOCryptoNewSHA256Callback = std::move(callback);
            aws_set_md5_new_fn(s_MD5New);
        }

        static struct aws_hash *s_Sha256New(struct aws_allocator *allocator)
        {
            auto hash = s_BYOCryptoNewSHA256Callback(AWS_SHA256_LEN, allocator);
            if (hash)
            {
                return hash->SeatForCInterop(hash);
            }
            else
            {
                return nullptr;
            }
        }

        void ApiHandle::SetBYOCryptoNewSHA256Callback(Crypto::CreateHashCallback &&callback)
        {
            s_BYOCryptoNewSHA256Callback = std::move(callback);
            aws_set_sha256_new_fn(s_Sha256New);
        }

        static struct aws_hmac *s_sha256HMACNew(struct aws_allocator *allocator, const struct aws_byte_cursor *secret)
        {
            auto hmac = s_BYOCryptoNewSHA256HMACCallback(AWS_SHA256_HMAC_LEN, *secret, allocator);
            return hmac->SeatForCInterop(hmac);
        }

        void ApiHandle::SetBYOCryptoNewSHA256HMACCallback(Crypto::CreateHMACCallback &&callback)
        {
            s_BYOCryptoNewSHA256HMACCallback = std::move(callback);
            aws_set_sha256_hmac_new_fn(s_sha256HMACNew);
        }

        static struct aws_channel_handler *s_NewClientTlsHandler(
            struct aws_allocator *allocator,
            struct aws_tls_connection_options *options,
            struct aws_channel_slot *slot,
            void *)
        {
            auto clientHandlerSelfReferencing = s_BYOCryptoNewClientTlsHandlerCallback(slot, *options, allocator);
            if (!clientHandlerSelfReferencing)
            {
                return nullptr;
            }
            return clientHandlerSelfReferencing->SeatForCInterop(clientHandlerSelfReferencing);
        }

        static int s_ClientTlsHandlerStartNegotiation(struct aws_channel_handler *handler, void *)
        {
            auto *clientHandler = reinterpret_cast<Io::ClientTlsChannelHandler *>(handler->impl);
            if (clientHandler->ChannelsThreadIsCallersThread())
            {
                clientHandler->StartNegotiation();
            }
            else
            {
                clientHandler->ScheduleTask([clientHandler](Io::TaskStatus) { clientHandler->StartNegotiation(); });
            }
            return AWS_OP_SUCCESS;
        }

        void ApiHandle::SetBYOCryptoClientTlsCallback(Io::NewClientTlsHandlerCallback &&callback)
        {
            s_BYOCryptoNewClientTlsHandlerCallback = std::move(callback);
            struct aws_tls_byo_crypto_setup_options setupOptions;
            setupOptions.new_handler_fn = s_NewClientTlsHandler;
            setupOptions.start_negotiation_fn = s_ClientTlsHandlerStartNegotiation;
            setupOptions.user_data = nullptr;
            aws_tls_byo_crypto_set_client_setup_options(&setupOptions);
        }

#else  // BYO_CRYPTO
        void ApiHandle::SetBYOCryptoClientTlsCallback(Io::NewClientTlsHandlerCallback &&)
        {
            AWS_LOGF_ERROR(AWS_LS_IO_TLS, "aws-crt-cpp must be compiled with BYO_CRYPTO=1");
        }

        void ApiHandle::SetBYOCryptoNewMD5Callback(Crypto::CreateHashCallback &&)
        {
            AWS_LOGF_ERROR(AWS_LS_IO_TLS, "aws-crt-cpp must be compiled with BYO_CRYPTO=1");
        }

        void ApiHandle::SetBYOCryptoNewSHA256Callback(Crypto::CreateHashCallback &&)
        {
            AWS_LOGF_ERROR(AWS_LS_IO_TLS, "aws-crt-cpp must be compiled with BYO_CRYPTO=1");
        }

        void ApiHandle::SetBYOCryptoNewSHA256HMACCallback(Crypto::CreateHMACCallback &&)
        {
            AWS_LOGF_ERROR(AWS_LS_IO_TLS, "aws-crt-cpp must be compiled with BYO_CRYPTO=1");
        }

        void ApiHandle::SetBYOCryptoClientTlsCallback(Io::NewClientTlsHandlerCallback &&)
        {
            AWS_LOGF_ERROR(AWS_LS_IO_TLS, "aws-crt-cpp must be compiled with BYO_CRYPTO=1");
        }

        void ApiHandle::SetBYOCryptoTlsContextCallbacks(
            Io::NewTlsContextImplCallback &&,
            Io::DeleteTlsContextImplCallback &&,
            Io::IsTlsAlpnSupportedCallback &&)
        {
            AWS_LOGF_ERROR(AWS_LS_IO_TLS, "aws-crt-cpp must be compiled with BYO_CRYPTO=1");
        }
#endif // BYO_CRYPTO

        const Io::NewTlsContextImplCallback &ApiHandle::GetBYOCryptoNewTlsContextImplCallback()
        {
            return s_BYOCryptoNewTlsContextImplCallback;
        }

        const Io::DeleteTlsContextImplCallback &ApiHandle::GetBYOCryptoDeleteTlsContextImplCallback()
        {
            return s_BYOCryptoDeleteTlsContextImplCallback;
        }

        const Io::IsTlsAlpnSupportedCallback &ApiHandle::GetBYOCryptoIsTlsAlpnSupportedCallback()
        {
            return s_BYOCryptoIsTlsAlpnSupportedCallback;
        }

        const char *ErrorDebugString(int error) noexcept { return aws_error_debug_str(error); }

        int LastError() noexcept { return aws_last_error(); }

        int LastErrorOrUnknown() noexcept
        {
            int last_error = aws_last_error();
            if (last_error == AWS_ERROR_SUCCESS)
            {
                last_error = AWS_ERROR_UNKNOWN;
            }

            return last_error;
        }

    } // namespace Crt
} // namespace Aws
