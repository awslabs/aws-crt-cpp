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
            using HttpHeader = aws_http_header;

            /*
             * Class representing a mutable http request.
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

                bool GetMethod(ByteCursor &method) const noexcept;
                bool SetMethod(ByteCursor method) noexcept;

                bool GetPath(ByteCursor &path) const noexcept;
                bool SetPath(ByteCursor path) noexcept;

                std::shared_ptr<Aws::Crt::Io::IStream> GetBody() const noexcept;
                bool SetBody(const std::shared_ptr<Aws::Crt::Io::IStream> &body) noexcept;

                size_t GetHeaderCount() const noexcept;
                bool GetHeader(size_t index, HttpHeader &header) const noexcept;
                bool SetHeader(size_t index, const HttpHeader &header) noexcept;
                bool AddHeader(const HttpHeader &header) noexcept;
                bool EraseHeader(size_t index) noexcept;

                operator bool() const noexcept { return m_request != nullptr; }

              private:
                Allocator *m_allocator;

                struct aws_http_request *m_request;
                std::shared_ptr<Aws::Crt::Io::IStream> m_bodyStream;
            };
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
