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
            TlsContextOptions::TlsContextOptions() noexcept { AWS_ZERO_STRUCT(m_options); }

            TlsContextOptions TlsContextOptions::InitDefaultClient() noexcept
            {
                TlsContextOptions ctxOptions;
                aws_tls_ctx_options_init_default_client(&ctxOptions.m_options, g_allocator);
                return ctxOptions;
            }

            TlsContextOptions TlsContextOptions::InitClientWithMtls(const char *certPath, const char *pKeyPath) noexcept
            {
                TlsContextOptions ctxOptions;
                aws_tls_ctx_options_init_client_mtls_from_path(
                    &ctxOptions.m_options, g_allocator, certPath, pKeyPath);
                return ctxOptions;
            }
#ifdef __APPLE__
            TlsContextOptions TlsContextOptions::InitClientWithMtlsPkcs12(
                const char *pkcs12Path,
                const char *pkcs12Pwd) noexcept
            {
                TlsContextOptions ctxOptions;
                struct aws_byte_cursor password = aws_byte_cursor_from_c_str(pkcs12Pwd);
                aws_tls_ctx_options_init_client_mtls_pkcs12_from_path(
                    &ctxOptions.m_options, g_allocator, pkcs12Path, &password);
                return ctxOptions;
            }
#endif

            bool TlsContextOptions::IsAlpnSupported() noexcept { return aws_tls_is_alpn_available(); }

            void TlsContextOptions::SetAlpnList(const char *alpn_list) noexcept
            {
                aws_tls_ctx_options_set_alpn_list(&m_options, alpn_list);
            }

            void TlsContextOptions::SetVerifyPeer(bool verify_peer) noexcept
            {
                aws_tls_ctx_options_set_verify_peer(&m_options, verify_peer);
            }

            void TlsContextOptions::OverrideDefaultTrustStore(const char *caPath, const char *caFile) noexcept
            {
                aws_tls_ctx_options_override_default_trust_store_from_path(&m_options, caPath, caFile);
            }

            void InitTlsStaticState(Aws::Crt::Allocator *alloc) noexcept { aws_tls_init_static_state(alloc); }

            void CleanUpTlsStaticState() noexcept { aws_tls_clean_up_static_state(); }

            TlsContext::TlsContext(TlsContextOptions &options, TlsMode mode, Allocator *allocator) noexcept
                : m_ctx(nullptr), m_lastError(AWS_OP_SUCCESS)
            {
                if (mode == TlsMode::CLIENT)
                {
                    m_ctx = aws_tls_client_ctx_new(allocator, &options.m_options);
                }
                else
                {
                    m_ctx = aws_tls_server_ctx_new(allocator, &options.m_options);
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

            TlsContext::TlsContext(TlsContext &&toMove) noexcept : m_ctx(toMove.m_ctx), m_lastError(toMove.m_lastError)
            {
                toMove.m_ctx = nullptr;
                toMove.m_lastError = AWS_ERROR_UNKNOWN;
            }

            TlsContext &TlsContext::operator=(TlsContext &&toMove) noexcept
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

            TlsContext::operator bool() const noexcept { return m_ctx && m_lastError == AWS_ERROR_SUCCESS; }

            int TlsContext::LastError() const noexcept { return m_lastError; }

            TlsConnectionOptions TlsContext::NewConnectionOptions() const noexcept
            {
                TlsConnectionOptions options;
                aws_tls_connection_options_init_from_ctx(&options, m_ctx);
                return options;
            }
        } // namespace Io
    }     // namespace Crt
} // namespace Aws
