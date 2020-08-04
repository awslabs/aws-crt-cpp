/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/io/TlsOptions.h>

#include <aws/crt/Api.h>
#include <aws/io/tls_channel_handler.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            TlsContextOptions::~TlsContextOptions()
            {
                if (m_isInit)
                {
                    aws_tls_ctx_options_clean_up(&m_options);
                }
            }

            TlsContextOptions::TlsContextOptions() noexcept : m_isInit(false) { AWS_ZERO_STRUCT(m_options); }

            TlsContextOptions::TlsContextOptions(TlsContextOptions &&other) noexcept
            {
                m_options = other.m_options;
                m_isInit = other.m_isInit;
                AWS_ZERO_STRUCT(other.m_options);
                other.m_isInit = false;
            }

            TlsContextOptions &TlsContextOptions::operator=(TlsContextOptions &&other) noexcept
            {
                if (&other != this)
                {
                    if (m_isInit)
                    {
                        aws_tls_ctx_options_clean_up(&m_options);
                    }

                    m_options = other.m_options;
                    m_isInit = other.m_isInit;
                    AWS_ZERO_STRUCT(other.m_options);
                    other.m_isInit = false;
                }

                return *this;
            }

            TlsContextOptions TlsContextOptions::InitDefaultClient(Allocator *allocator) noexcept
            {
                TlsContextOptions ctxOptions;
                aws_tls_ctx_options_init_default_client(&ctxOptions.m_options, allocator);
                ctxOptions.m_isInit = true;
                return ctxOptions;
            }

            TlsContextOptions TlsContextOptions::InitClientWithMtls(
                const char *certPath,
                const char *pKeyPath,
                Allocator *allocator) noexcept
            {
                TlsContextOptions ctxOptions;
                if (!aws_tls_ctx_options_init_client_mtls_from_path(
                        &ctxOptions.m_options, allocator, certPath, pKeyPath))
                {
                    ctxOptions.m_isInit = true;
                }
                return ctxOptions;
            }

            TlsContextOptions TlsContextOptions::InitClientWithMtls(
                const ByteCursor &cert,
                const ByteCursor &pkey,
                Allocator *allocator) noexcept
            {
                TlsContextOptions ctxOptions;
                if (!aws_tls_ctx_options_init_client_mtls(
                        &ctxOptions.m_options,
                        allocator,
                        const_cast<ByteCursor *>(&cert),
                        const_cast<ByteCursor *>(&pkey)))
                {
                    ctxOptions.m_isInit = true;
                }
                return ctxOptions;
            }
#ifdef __APPLE__
            TlsContextOptions TlsContextOptions::InitClientWithMtlsPkcs12(
                const char *pkcs12Path,
                const char *pkcs12Pwd,
                Allocator *allocator) noexcept
            {
                TlsContextOptions ctxOptions;
                struct aws_byte_cursor password = aws_byte_cursor_from_c_str(pkcs12Pwd);
                if (!aws_tls_ctx_options_init_client_mtls_pkcs12_from_path(
                        &ctxOptions.m_options, allocator, pkcs12Path, &password))
                {
                    ctxOptions.m_isInit = true;
                }
                return ctxOptions;
            }
#endif

            bool TlsContextOptions::IsAlpnSupported() noexcept { return aws_tls_is_alpn_available(); }

            bool TlsContextOptions::SetAlpnList(const char *alpn_list) noexcept
            {
                AWS_ASSERT(m_isInit);
                return aws_tls_ctx_options_set_alpn_list(&m_options, alpn_list) == 0;
            }

            void TlsContextOptions::SetVerifyPeer(bool verify_peer) noexcept
            {
                AWS_ASSERT(m_isInit);
                aws_tls_ctx_options_set_verify_peer(&m_options, verify_peer);
            }

            void TlsContextOptions::SetMinimumTlsVersion(aws_tls_versions minimumTlsVersion)
            {
                AWS_ASSERT(m_isInit);
                aws_tls_ctx_options_set_minimum_tls_version(&m_options, minimumTlsVersion);
            }

            bool TlsContextOptions::OverrideDefaultTrustStore(const char *caPath, const char *caFile) noexcept
            {
                AWS_ASSERT(m_isInit);
                return aws_tls_ctx_options_override_default_trust_store_from_path(&m_options, caPath, caFile) == 0;
            }

            bool TlsContextOptions::OverrideDefaultTrustStore(const ByteCursor &ca) noexcept
            {
                AWS_ASSERT(m_isInit);
                return aws_tls_ctx_options_override_default_trust_store(&m_options, const_cast<ByteCursor *>(&ca)) == 0;
            }

            TlsConnectionOptions::TlsConnectionOptions() noexcept : m_lastError(AWS_ERROR_SUCCESS), m_isInit(false) {}

            TlsConnectionOptions::TlsConnectionOptions(aws_tls_ctx *ctx, Allocator *allocator) noexcept
                : m_allocator(allocator), m_lastError(AWS_ERROR_SUCCESS), m_isInit(true)
            {
                aws_tls_connection_options_init_from_ctx(&m_tls_connection_options, ctx);
            }

            TlsConnectionOptions::~TlsConnectionOptions()
            {
                if (m_isInit)
                {
                    aws_tls_connection_options_clean_up(&m_tls_connection_options);
                    m_isInit = false;
                }
            }

            TlsConnectionOptions::TlsConnectionOptions(const TlsConnectionOptions &options) noexcept
            {
                m_isInit = false;

                if (options.m_isInit)
                {
                    m_allocator = options.m_allocator;
                    if (!aws_tls_connection_options_copy(&m_tls_connection_options, &options.m_tls_connection_options))
                    {
                        m_isInit = true;
                    }
                    else
                    {
                        m_lastError = LastErrorOrUnknown();
                    }
                }
            }

            TlsConnectionOptions &TlsConnectionOptions::operator=(const TlsConnectionOptions &options) noexcept
            {
                if (this != &options)
                {
                    if (m_isInit)
                    {
                        aws_tls_connection_options_clean_up(&m_tls_connection_options);
                    }

                    m_isInit = false;

                    if (options.m_isInit)
                    {
                        m_allocator = options.m_allocator;
                        if (!aws_tls_connection_options_copy(
                                &m_tls_connection_options, &options.m_tls_connection_options))
                        {
                            m_isInit = true;
                        }
                        else
                        {
                            m_lastError = LastErrorOrUnknown();
                        }
                    }
                }

                return *this;
            }

            TlsConnectionOptions::TlsConnectionOptions(TlsConnectionOptions &&options) noexcept
                : m_isInit(options.m_isInit)
            {
                if (options.m_isInit)
                {
                    m_tls_connection_options = options.m_tls_connection_options;
                    AWS_ZERO_STRUCT(options.m_tls_connection_options);
                    options.m_isInit = false;
                    options.m_allocator = options.m_allocator;
                }
            }

            TlsConnectionOptions &TlsConnectionOptions::operator=(TlsConnectionOptions &&options) noexcept
            {
                if (this != &options)
                {
                    if (options.m_isInit)
                    {
                        m_tls_connection_options = options.m_tls_connection_options;
                        AWS_ZERO_STRUCT(options.m_tls_connection_options);
                        options.m_isInit = false;
                        m_isInit = true;
                        m_allocator = options.m_allocator;
                    }
                }

                return *this;
            }

            bool TlsConnectionOptions::SetServerName(ByteCursor &serverName) noexcept
            {
                if (aws_tls_connection_options_set_server_name(&m_tls_connection_options, m_allocator, &serverName))
                {
                    m_lastError = LastErrorOrUnknown();
                    return false;
                }

                return true;
            }

            bool TlsConnectionOptions::SetAlpnList(const char *alpnList) noexcept
            {
                if (aws_tls_connection_options_set_alpn_list(&m_tls_connection_options, m_allocator, alpnList))
                {
                    m_lastError = LastErrorOrUnknown();
                    return false;
                }

                return true;
            }

            TlsContext::TlsContext() noexcept : m_ctx(nullptr), m_initializationError(AWS_ERROR_SUCCESS) {}

            TlsContext::TlsContext(TlsContextOptions &options, TlsMode mode, Allocator *allocator) noexcept
                : m_ctx(nullptr), m_initializationError(AWS_ERROR_SUCCESS)
            {
                if (mode == TlsMode::CLIENT)
                {
                    m_ctx.reset(aws_tls_client_ctx_new(allocator, &options.m_options), aws_tls_ctx_destroy);
                }
                else
                {
                    m_ctx.reset(aws_tls_server_ctx_new(allocator, &options.m_options), aws_tls_ctx_destroy);
                }

                if (!m_ctx)
                {
                    m_initializationError = Aws::Crt::LastErrorOrUnknown();
                }
            }

            TlsConnectionOptions TlsContext::NewConnectionOptions() const noexcept
            {
                return TlsConnectionOptions(m_ctx.get(), m_ctx->alloc);
            }
        } // namespace Io
    }     // namespace Crt
} // namespace Aws
