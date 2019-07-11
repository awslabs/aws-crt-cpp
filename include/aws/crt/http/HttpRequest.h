#pragma once
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

#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>

struct aws_http_header;
struct aws_http_request;

namespace Aws
{
    namespace Crt
    {
        namespace Http
        {
            using HttpHeader = struct aws_http_header;

            /*
             *
             */
            class AWS_CRT_CPP_API HttpRequest
            {
              public:
                HttpRequest(Allocator *allocator = DefaultAllocator()) noexcept;
                ~HttpRequest();

                HttpRequest(const HttpRequest &) = delete;
                HttpRequest(HttpRequest &&) = delete;
                HttpRequest &operator=(const HttpRequest &) = delete;
                HttpRequest &operator=(HttpRequest &&) = delete;

                ByteCursor GetMethod(void) const noexcept;
                void SetMethod(ByteCursor method) noexcept;

                ByteCursor GetPath(void) const noexcept;
                void SetPath(ByteCursor path) noexcept;

                std::shared_ptr<Aws::Crt::Io::IStream> GetBody(void) const noexcept;
                void SetBody(const std::shared_ptr<Aws::Crt::Io::IStream> &body) noexcept;

                size_t GetHeaderCount(void) const noexcept;
                HttpHeader GetHeader(size_t index) const noexcept;
                void SetHeader(size_t index, const HttpHeader &header) noexcept;
                void AddHeader(const HttpHeader &header) noexcept;
                void EraseHeader(size_t index) noexcept;

                operator bool() const noexcept { return m_request != nullptr; }

              private:
                Allocator *m_allocator;

                struct aws_http_request *m_request;
                std::shared_ptr<Aws::Crt::Io::IStream> m_bodyStream;
            };
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
