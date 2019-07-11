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

#include <aws/crt/http/HttpRequest.h>

#include <aws/crt/io/Stream.h>
#include <aws/http/request_response.h>

namespace Aws
{
    namespace Crt
    {
        namespace Http
        {

            HttpRequest::HttpRequest(Allocator *allocator) noexcept
                : m_allocator(allocator), m_request(aws_http_request_new(allocator)), m_bodyStream(nullptr)
            {
            }

            HttpRequest::~HttpRequest() { aws_http_request_destroy(m_request); }

            Optional<ByteCursor> HttpRequest::GetMethod() const noexcept
            {
                ByteCursor method;
                if (aws_http_request_get_method(m_request, &method) != AWS_OP_SUCCESS)
                {
                    return Optional<ByteCursor>();
                }

                return Optional<ByteCursor>(method);
            }

            bool HttpRequest::SetMethod(ByteCursor method) noexcept
            {
                return aws_http_request_set_method(m_request, method) == AWS_OP_SUCCESS;
            }

            Optional<ByteCursor> HttpRequest::GetPath() const noexcept
            {
                ByteCursor path;
                if (aws_http_request_get_path(m_request, &path) != AWS_OP_SUCCESS)
                {
                    return Optional<ByteCursor>();
                }

                return Optional<ByteCursor>(path);
            }

            bool HttpRequest::SetPath(ByteCursor path) noexcept
            {
                return aws_http_request_set_path(m_request, path) == AWS_OP_SUCCESS;
            }

            std::shared_ptr<Aws::Crt::Io::IStream> HttpRequest::GetBody() const noexcept { return m_bodyStream; }

            bool HttpRequest::SetBody(const std::shared_ptr<Aws::Crt::Io::IStream> &body) noexcept
            {
                aws_input_stream *stream = nullptr;
                if (body != nullptr)
                {
                    stream = Aws::Crt::Io::AwsInputStreamNewCpp(body, m_allocator);
                    if (stream == nullptr)
                    {
                        return false;
                    }
                }

                aws_http_request_set_body_stream(m_request, stream);

                m_bodyStream = (stream) ? body : nullptr;

                return true;
            }

            size_t HttpRequest::GetHeaderCount() const noexcept { return aws_http_request_get_header_count(m_request); }

            Optional<HttpHeader> HttpRequest::GetHeader(size_t index) const noexcept
            {
                HttpHeader header;
                if (aws_http_request_get_header(m_request, &header, index) != AWS_OP_SUCCESS)
                {
                    return Optional<HttpHeader>();
                }

                return Optional<HttpHeader>(header);
            }

            bool HttpRequest::SetHeader(size_t index, const HttpHeader &header) noexcept
            {
                return aws_http_request_set_header(m_request, header, index) == AWS_OP_SUCCESS;
            }

            bool HttpRequest::AddHeader(const HttpHeader &header) noexcept
            {
                return aws_http_request_add_header(m_request, header) == AWS_OP_SUCCESS;
            }

            bool HttpRequest::EraseHeader(size_t index) noexcept
            {
                return aws_http_request_erase_header(m_request, index) == AWS_OP_SUCCESS;
            }
        } // namespace Http
    }     // namespace Crt
} // namespace Aws
