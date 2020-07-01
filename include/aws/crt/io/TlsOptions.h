#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Types.h>

#include <aws/io/tls_channel_handler.h>

struct aws_tls_ctx_options;

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            enum class TlsMode
            {
                CLIENT,
                SERVER,
            };

            class AWS_CRT_CPP_API TlsContextOptions final
            {
                friend class TlsContext;

              public:
                TlsContextOptions() noexcept;
                ~TlsContextOptions();
                TlsContextOptions(const TlsContextOptions &) noexcept = delete;
                TlsContextOptions &operator=(const TlsContextOptions &) noexcept = delete;
                TlsContextOptions(TlsContextOptions &&) noexcept;
                TlsContextOptions &operator=(TlsContextOptions &&) noexcept;
                /**
                 * @return true if the instance is in a valid state, false otherwise.
                 */
                explicit operator bool() const noexcept { return m_isInit; }
                /**
                 * @return the value of the last aws error encountered by operations on this instance.
                 */
                int LastError() const noexcept { return aws_last_error(); }

                /**
                 * Initializes TlsContextOptions with secure by default options, with
                 * no client certificates.
                 */
                static TlsContextOptions InitDefaultClient(Allocator *allocator = g_allocator) noexcept;
                /**
                 * Initializes TlsContextOptions with secure by default options, with
                 * client certificate and private key. These are paths to a file on disk. These files
                 * must be in the PEM format.
                 * @param cert_path: Path to certificate file.
                 * @param pkey_path: Path to private key file.
                 */
                static TlsContextOptions InitClientWithMtls(
                    const char *cert_path,
                    const char *pkey_path,
                    Allocator *allocator = g_allocator) noexcept;

                /**
                 * Initializes TlsContextOptions with secure by default options, with
                 * client certificate and private key. These are in memory buffers. These buffers
                 * must be in the PEM format.
                 * @param cert: Certificate contents in memory.
                 * @param pkey: Private key contents in memory.
                 */
                static TlsContextOptions InitClientWithMtls(
                    const ByteCursor &cert,
                    const ByteCursor &pkey,
                    Allocator *allocator = g_allocator) noexcept;

#ifdef __APPLE__
                /**
                 * Initializes TlsContextOptions with secure by default options, with
                 * client certificateand private key in the PKCS#12 format.
                 * NOTE: This configuration only works on Apple devices.
                 * @param pkcs12_path: Path to PKCS #12 file. The file is loaded from disk and stored internally. It
                 * must remain in memory for the lifetime of the returned object.
                 * @param pkcs12_pwd: Password to PKCS #12 file. It must remain in memory for the lifetime of the
                 * returned object.
                 */
                static TlsContextOptions InitClientWithMtlsPkcs12(
                    const char *pkcs12_path,
                    const char *pkcs12_pwd,
                    Allocator *allocator = g_allocator) noexcept;
#endif

                /**
                 * @return true if alpn is supported by the underlying security provider, false
                 * otherwise.
                 */
                static bool IsAlpnSupported() noexcept;

                /**
                 * Sets the list of alpn protocols.
                 * @param alpnList: List of protocol names, delimited by ';'. This string must remain in memory for the
                 * lifetime of this object.
                 */
                bool SetAlpnList(const char *alpnList) noexcept;

                /**
                 * In client mode, this turns off x.509 validation. Don't do this unless you're testing.
                 * It's much better, to just override the default trust store and pass the self-signed
                 * certificate as the caFile argument.
                 *
                 * In server mode, this defaults to false. If you want to support mutual TLS from the server,
                 * you'll want to set this to true.
                 */
                void SetVerifyPeer(bool verifyPeer) noexcept;

                /**
                 * Sets the minimum TLS version allowed.
                 * @param minimumTlsVersion: The minimum TLS version.
                 */
                void SetMinimumTlsVersion(aws_tls_versions minimumTlsVersion);

                /**
                 * Overrides the default system trust store.
                 * @param caPath: Path to directory containing trusted certificates, which will overrides the
                 * default trust store. Only useful on Unix style systems where all anchors are stored in a directory
                 * (like /etc/ssl/certs). This string must remain in memory for the lifetime of this object.
                 * @param caFile: Path to file containing PEM armored chain of trusted CA certificates. This
                 * string must remain in memory for the lifetime of this object.
                 */
                bool OverrideDefaultTrustStore(const char *caPath, const char *caFile) noexcept;
                /**
                 * Overrides the default system trust store.
                 * @param ca: PEM armored chain of trusted CA certificates.
                 */
                bool OverrideDefaultTrustStore(const ByteCursor &ca) noexcept;

              private:
                aws_tls_ctx_options m_options;
                bool m_isInit;
            };

            /**
             * Options specific to a single connection.
             */
            class AWS_CRT_CPP_API TlsConnectionOptions final
            {
              public:
                TlsConnectionOptions() noexcept;
                ~TlsConnectionOptions();
                TlsConnectionOptions(const TlsConnectionOptions &) noexcept;
                TlsConnectionOptions &operator=(const TlsConnectionOptions &) noexcept;
                TlsConnectionOptions(TlsConnectionOptions &&options) noexcept;
                TlsConnectionOptions &operator=(TlsConnectionOptions &&options) noexcept;

                /**
                 * Sets SNI extension, and also the name used for X.509 validation. serverName is copied.
                 *
                 * @return true if the copy succeeded, or false otherwise.
                 */
                bool SetServerName(ByteCursor &serverName) noexcept;

                /**
                 * Sets list of protocols (semi-colon delimited in priority order) used for ALPN extension.
                 * alpnList is copied.
                 *
                 * @return true if the copy succeeded, or false otherwise.
                 */
                bool SetAlpnList(const char *alpnList) noexcept;
                /**
                 * @return true if the instance is in a valid state, false otherwise.
                 */
                explicit operator bool() const noexcept { return m_isInit; }
                /**
                 * @return the value of the last aws error encountered by operations on this instance.
                 */
                int LastError() const noexcept { return m_lastError; }
                /// @private
                const aws_tls_connection_options *GetUnderlyingHandle() const noexcept
                {
                    return &m_tls_connection_options;
                }

              private:
                TlsConnectionOptions(aws_tls_ctx *ctx, Allocator *allocator) noexcept;
                aws_tls_connection_options m_tls_connection_options;
                aws_allocator *m_allocator;
                int m_lastError;
                bool m_isInit;

                friend class TlsContext;
            };

            class AWS_CRT_CPP_API TlsContext final
            {
              public:
                TlsContext() noexcept;
                TlsContext(TlsContextOptions &options, TlsMode mode, Allocator *allocator = g_allocator) noexcept;
                ~TlsContext() = default;
                TlsContext(const TlsContext &) noexcept = default;
                TlsContext &operator=(const TlsContext &) noexcept = default;
                TlsContext(TlsContext &&) noexcept = default;
                TlsContext &operator=(TlsContext &&) noexcept = default;

                TlsConnectionOptions NewConnectionOptions() const noexcept;
                /**
                 * @return true if the instance is in a valid state, false otherwise.
                 */
                explicit operator bool() const noexcept { return m_ctx && m_initializationError == AWS_ERROR_SUCCESS; }
                /**
                 * @return the value of the last aws error encountered by operations on this instance.
                 */
                int GetInitializationError() const noexcept { return m_initializationError; }

              private:
                std::shared_ptr<aws_tls_ctx> m_ctx;
                int m_initializationError;
            };

            AWS_CRT_CPP_API void InitTlsStaticState(Allocator *alloc) noexcept;
            AWS_CRT_CPP_API void CleanUpTlsStaticState() noexcept;

        } // namespace Io
    }     // namespace Crt
} // namespace Aws
