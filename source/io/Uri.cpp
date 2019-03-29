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
#include <aws/crt/io/Uri.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            Uri::Uri() noexcept : m_lastError(AWS_ERROR_SUCCESS), m_isInit(false) { AWS_ZERO_STRUCT(m_uri); }

            Uri::~Uri()
            {
                if (m_isInit)
                {
                    aws_uri_clean_up(&m_uri);
                    m_isInit = false;
                }
            }

            Uri::Uri(const ByteCursor &cursor, Allocator *allocator) noexcept
                : m_lastError(AWS_ERROR_SUCCESS), m_isInit(false)
            {
                if (!aws_uri_init_parse(&m_uri, allocator, &cursor))
                {
                    m_isInit = true;
                }
                else
                {
                    m_lastError = aws_last_error();
                }
            }

            Uri::Uri(aws_uri_builder_options &builderOptions, Allocator *allocator) noexcept
                : m_lastError(AWS_ERROR_SUCCESS), m_isInit(false)
            {
                if (!aws_uri_init_from_builder_options(&m_uri, allocator, &builderOptions))
                {
                    m_isInit = true;
                }
                else
                {
                    m_lastError = aws_last_error();
                }
            }

            Uri::Uri(Uri &&uri) noexcept : m_lastError(AWS_ERROR_SUCCESS), m_isInit(uri.m_isInit)
            {
                if (uri.m_isInit)
                {
                    m_uri = uri.m_uri;
                    AWS_ZERO_STRUCT(uri.m_uri);
                    uri.m_isInit = false;
                }
            }

            Uri &Uri::operator=(Uri &&uri) noexcept
            {
                if (this != &uri)
                {
                    if (uri.m_isInit)
                    {
                        m_uri = uri.m_uri;
                        AWS_ZERO_STRUCT(uri.m_uri);
                        uri.m_isInit = false;
                        m_isInit = true;
                        m_lastError = AWS_ERROR_SUCCESS;
                    }
                }

                return *this;
            }

            ByteCursor Uri::GetScheme() const noexcept { return m_uri.scheme; }

            ByteCursor Uri::GetAuthority() const noexcept { return m_uri.authority; }

            ByteCursor Uri::GetPath() const noexcept { return m_uri.path; }

            ByteCursor Uri::GetQueryString() const noexcept { return m_uri.query_string; }

            ByteCursor Uri::GetHostName() const noexcept { return m_uri.host_name; }

            uint16_t Uri::GetPort() const noexcept { return m_uri.port; }

            ByteCursor Uri::GetPathAndQuery() const noexcept { return m_uri.path_and_query; }

            const ByteBuf &Uri::GetFullUri() const noexcept { return m_uri.uri_str; }
        } // namespace Io
    }     // namespace Crt
} // namespace Aws