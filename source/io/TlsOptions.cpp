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
#include <aws/crt/io/TlsOptions.h>

#include <aws/io/tls_channel_handler.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            void InitDefaultClient(TlsContextOptions& options) noexcept
            {
                aws_tls_ctx_options_init_default_client(&options);
            }

            void InitClientWithMtls(TlsContextOptions &options,
                                    const char *certPath, const char *pKeyPath) noexcept
            {
                aws_tls_ctx_options_init_client_mtls(&options, certPath, pKeyPath);
            }

            void InitClientWithMtlsPkcs12(TlsContextOptions &options,
                                          const char *pkcs12Path, const char *pkcs12Pwd) noexcept
            {
                aws_tls_ctx_options_init_client_mtls_pkcs12(&options, pkcs12Path, pkcs12Pwd);
            }

            void SetALPNList(TlsContextOptions& options, const char* alpn_list) noexcept
            {
                aws_tls_ctx_options_set_alpn_list(&options, alpn_list);
            }

            void SetVerifyPeer(TlsContextOptions& options, bool verify_peer) noexcept
            {
                aws_tls_ctx_options_set_verify_peer(&options, verify_peer);
            }

            void OverrideDefaultTrustStore(TlsContextOptions& options,
                const char* caPath, const char* caFile) noexcept
            {
                aws_tls_ctx_options_override_default_trust_store(&options, caPath, caFile);
            }

            void InitTlsStaticState(Aws::Crt::Allocator *alloc) noexcept
            {
                aws_tls_init_static_state(alloc);
            }

            void CleanUpTlsStaticState() noexcept
            {
                aws_tls_clean_up_static_state();
            }

            bool IsAlpnSupported() noexcept
            {
                return aws_tls_is_alpn_available();
            }

            TlsContext::TlsContext(TlsContextOptions& options, TLSMode mode, Allocator* allocator) noexcept :
                m_ctx(nullptr), m_lastError(AWS_OP_SUCCESS)
            {
                if (mode == TLSMode::CLIENT)
                {
                    m_ctx = aws_tls_client_ctx_new(allocator, &options);
                }
                else
                {
                    m_ctx = aws_tls_server_ctx_new(allocator, &options);
                }

                if (!m_ctx)
                {
                    m_lastError = aws_last_error();
                }
            }

            TlsContext::~TlsContext()
            {
                if (*this)
                {
                    aws_tls_ctx_destroy(m_ctx);
                }
            }

            TlsContext::TlsContext(TlsContext&& toMove) noexcept :
                m_ctx(toMove.m_ctx),
                m_lastError(toMove.m_lastError)
            {
                toMove.m_ctx = nullptr;
                toMove.m_lastError = AWS_ERROR_UNKNOWN;
            }

            TlsContext& TlsContext::operator=(TlsContext&& toMove) noexcept
            {
                if (this == &toMove)
                {
                    return *this;
                }

                m_ctx = toMove.m_ctx;
                m_lastError = toMove.m_lastError;
                toMove.m_ctx = nullptr;
                toMove.m_lastError = AWS_ERROR_UNKNOWN;

                return *this;
            }

            TlsContext::operator bool() const noexcept
            {
                return m_ctx && m_lastError == AWS_ERROR_SUCCESS;
            }

            int TlsContext::LastError() const noexcept
            {
                return m_lastError;
            }

            TlsConnectionOptions TlsContext::NewConnectionOptions() const noexcept
            {
                TlsConnectionOptions options;
                aws_tls_connection_options_init_from_ctx(&options, m_ctx);
                return options;
            }
        }
    }
}
