/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/StlAllocator.h>
#include <aws/crt/external/cJSON.h>
#include <aws/crt/io/TlsOptions.h>

#include <aws/auth/auth.h>
#include <aws/http/http.h>
#include <aws/mqtt/mqtt.h>

namespace Aws
{
    namespace Crt
    {
        Allocator *g_allocator = Aws::Crt::DefaultAllocator();

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

        static void s_cleanUpApi()
        {
            g_allocator = nullptr;
            aws_auth_library_clean_up();
            aws_mqtt_library_clean_up();
            aws_http_library_clean_up();
        }

        ApiHandle::ApiHandle(Allocator *allocator) noexcept : logger() { s_initApi(allocator); }

        ApiHandle::ApiHandle() noexcept : logger() { s_initApi(DefaultAllocator()); }

        ApiHandle::~ApiHandle()
        {
            s_cleanUpApi();

            if (aws_logger_get() == &logger)
            {
                aws_logger_set(NULL);
                aws_logger_clean_up(&logger);
            }
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
