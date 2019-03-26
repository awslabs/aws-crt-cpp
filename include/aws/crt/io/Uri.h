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
#include <aws/crt/Types.h>

#include <aws/io/uri.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            class AWS_CRT_CPP_API Uri final
            {
              public:
                Uri() noexcept;
                ~Uri();
                Uri(const ByteCursor &cursor, Allocator *allocator = DefaultAllocator()) noexcept;
                Uri(aws_uri_builder_options &builderOptions, Allocator *allocator = DefaultAllocator()) noexcept;
                Uri(const Uri &) = delete;
                Uri &operator=(const Uri &) = delete;
                Uri(Uri &&uri) noexcept;
                Uri &operator=(Uri &&) noexcept;

                operator bool() const noexcept { return m_isInit; }
                int LastError() const noexcept { return m_lastError; }

                ByteCursor GetScheme() const noexcept;
                ByteCursor GetAuthority() const noexcept;
                ByteCursor GetPath() const noexcept;
                ByteCursor GetQueryString() const noexcept;
                ByteCursor GetHostName() const noexcept;
                uint16_t GetPort() const noexcept;
                ByteCursor GetPathAndQuery() const noexcept;
                const ByteBuf &GetFullUri() const noexcept;

              private:
                aws_uri m_uri;
                int m_lastError;
                bool m_isInit;
            };
        } // namespace Io
    }     // namespace Crt
} // namespace Aws