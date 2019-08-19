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
struct aws_http_message;

namespace Aws
{
    namespace Crt
    {
        namespace Http
        {
            using HttpHeader = aws_http_header;

            /*
             * Base class representing a mutable http request or response.
             */
            class AWS_CRT_CPP_API HttpMessage
            {
              public:
                virtual ~HttpMessage();

                HttpMessage(const HttpMessage &) = delete;
                HttpMessage(HttpMessage &&) = delete;
                HttpMessage &operator=(const HttpMessage &) = delete;
                HttpMessage &operator=(HttpMessage &&) = delete;

                std::shared_ptr<Aws::Crt::Io::IStream> GetBody() const noexcept;
                bool SetBody(const std::shared_ptr<Aws::Crt::Io::IStream> &body) noexcept;

                size_t GetHeaderCount() const noexcept;
                Optional<HttpHeader> GetHeader(size_t index) const noexcept;
                bool SetHeader(size_t index, const HttpHeader &header) noexcept;
                bool AddHeader(const HttpHeader &header) noexcept;
                bool EraseHeader(size_t index) noexcept;

                operator bool() const noexcept { return m_message != nullptr; }

                struct aws_http_message *GetUnderlyingMessage() const noexcept { return m_message; }

              protected:
                HttpMessage(Allocator *allocator, struct aws_http_message *message) noexcept;

                Allocator *m_allocator;
                struct aws_http_message *m_message;
                std::shared_ptr<Aws::Crt::Io::IStream> m_bodyStream;
            };

            /*
             * Class representing a mutable http request.
             */
            class AWS_CRT_CPP_API HttpRequest : public HttpMessage
            {
              public:
                HttpRequest(Allocator *allocator = DefaultAllocator());

                Optional<ByteCursor> GetMethod() const noexcept;
                bool SetMethod(ByteCursor method) noexcept;

                Optional<ByteCursor> GetPath() const noexcept;
                bool SetPath(ByteCursor path) noexcept;
            };

            /*
             * Class representing a mutable http response.
             */
            class AWS_CRT_CPP_API HttpResponse : public HttpMessage
            {
              public:
                HttpResponse(Allocator *allocator = DefaultAllocator());

                Optional<int> GetResponse() const noexcept;
                bool SetResponse(int response) noexcept;
            };
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
