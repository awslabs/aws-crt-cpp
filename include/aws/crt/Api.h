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
#include <aws/crt/Types.h>
#include <aws/crt/io/Bootstrap.h>
#include <aws/crt/io/EventLoopGroup.h>
#include <aws/crt/io/TlsOptions.h>
#include <aws/crt/mqtt/MqttClient.h>

#include <aws/common/logging.h>

namespace Aws
{
    namespace Crt
    {
        enum class LogLevel
        {
            None = AWS_LL_NONE,
            Fatal = AWS_LL_FATAL,
            Error = AWS_LL_ERROR,
            Warn = AWS_LL_WARN,
            Info = AWS_LL_INFO,
            Debug = AWS_LL_DEBUG,
            Trace = AWS_LL_TRACE,

            Count
        };

        class AWS_CRT_CPP_API ApiHandle
        {
          public:
            ApiHandle(Allocator *allocator) noexcept;
            ApiHandle() noexcept;
            ~ApiHandle();
            ApiHandle(const ApiHandle &) = delete;
            ApiHandle(ApiHandle &&) = delete;
            ApiHandle &operator=(const ApiHandle &) = delete;
            ApiHandle &operator=(ApiHandle &&) = delete;

            void InitializeLogging(LogLevel level, const char *filename);
            void InitializeLogging(LogLevel level, FILE *fp);

          private:
            void InitializeLoggingCommon(struct aws_logger_standard_options &options);

            aws_logger logger;
        };

        AWS_CRT_CPP_API const char *ErrorDebugString(int error) noexcept;
        AWS_CRT_CPP_API int LastError() noexcept;
        AWS_CRT_CPP_API int LastErrorOrUnknown() noexcept;
    } // namespace Crt
} // namespace Aws
