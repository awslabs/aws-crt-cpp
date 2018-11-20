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
#include <aws/crt/io/Bootstrap.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            ClientBootstrap::ClientBootstrap(const EventLoopGroup& elGroup, Allocator* allocator) noexcept :
                m_lastError(AWS_ERROR_SUCCESS)
            {
                AWS_ZERO_STRUCT(m_bootstrap);
                if (aws_client_bootstrap_init(&m_bootstrap, allocator,
                        (aws_event_loop_group*)elGroup.GetUnderlyingHandle(), nullptr, nullptr))
                {
                    m_lastError = aws_last_error();
                }
            }

            ClientBootstrap::~ClientBootstrap()
            {
                if (*this)
                {
                    aws_client_bootstrap_clean_up(&m_bootstrap);
                    m_lastError = AWS_ERROR_UNKNOWN;
                    AWS_ZERO_STRUCT(m_bootstrap);
                }
            }

            ClientBootstrap::ClientBootstrap(ClientBootstrap&& toMove) noexcept :
                m_bootstrap(toMove.m_bootstrap),
                m_lastError(toMove.m_lastError)
            {
                toMove.m_lastError = AWS_ERROR_UNKNOWN;
                AWS_ZERO_STRUCT(toMove.m_bootstrap);
            }

            ClientBootstrap& ClientBootstrap::operator=(ClientBootstrap&& toMove) noexcept
            {
                if (this == &toMove)
                {
                    return *this;
                }

                m_bootstrap = toMove.m_bootstrap;
                m_lastError = toMove.m_lastError;
                toMove.m_lastError = AWS_ERROR_UNKNOWN;
                AWS_ZERO_STRUCT(toMove.m_bootstrap);

                return *this;
            }

            ClientBootstrap::operator bool() noexcept
            {
                return m_lastError == AWS_ERROR_SUCCESS;
            }

            int ClientBootstrap::LastError() noexcept
            {
                return m_lastError;
            }

            const aws_client_bootstrap* ClientBootstrap::GetUnderlyingHandle() const
            {
                return &m_bootstrap;
            }
        }
    }
}
