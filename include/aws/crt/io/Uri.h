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
            /**
             * Contains a URI used for networking application protocols. This type is move-only.
             */
            class AWS_CRT_CPP_API Uri final
            {
              public:
                Uri() noexcept;
                ~Uri();
                /**
                 * Parses `cursor` as a URI. Upon failure the bool() operator will return false and LastError()
                 * will contain the errorCode.
                 */
                Uri(const ByteCursor &cursor, Allocator *allocator = g_allocator) noexcept;
                /**
                 * builds a URI from `builderOptions`. Upon failure the bool() operator will return false and
                 * LastError() will contain the errorCode.
                 */
                Uri(aws_uri_builder_options &builderOptions, Allocator *allocator = g_allocator) noexcept;
                Uri(const Uri &);
                Uri &operator=(const Uri &);
                Uri(Uri &&uri) noexcept;
                Uri &operator=(Uri &&) noexcept;

                operator bool() const noexcept { return m_isInit; }
                int LastError() const noexcept { return m_lastError; }

                /**
                 * Returns the scheme portion of the URI if present (e.g. https, http, ftp etc....)
                 */
                ByteCursor GetScheme() const noexcept;

                /**
                 * Returns the authority portion of the URI if present. This will contain host name and port if
                 * specified.
                 * */
                ByteCursor GetAuthority() const noexcept;

                /**
                 * Returns the path portion of the URI. If no path was present, this will be set to '/'.
                 */
                ByteCursor GetPath() const noexcept;

                /**
                 * Returns the query string portion of the URI if present.
                 */
                ByteCursor GetQueryString() const noexcept;

                /**
                 * Returns the host name portion of the authority. (port will not be in this value).
                 */
                ByteCursor GetHostName() const noexcept;

                /**
                 * Returns the port portion of the authority if a port was specified. If it was not, this will
                 * be set to 0. In that case, it is your responsibility to determine the correct port
                 * based on the protocol you're using.
                 */
                uint16_t GetPort() const noexcept;

                /** Returns the Path and Query portion of the URI. In the case of Http, this likely the value for the
                 * URI parameter.
                 */
                ByteCursor GetPathAndQuery() const noexcept;

                /**
                 * The full URI as it was passed to or parsed from the constructors.
                 */
                ByteCursor GetFullUri() const noexcept;

              private:
                aws_uri m_uri;
                int m_lastError;
                bool m_isInit;
            };
        } // namespace Io
    }     // namespace Crt
} // namespace Aws
