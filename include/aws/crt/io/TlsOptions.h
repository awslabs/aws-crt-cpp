#pragma once
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

#include <aws/crt/Types.h>

#include <aws/io/tls_channel_handler.h>

struct aws_tls_ctx_options;

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            using TlsConnectionOptions = aws_tls_connection_options;

            enum class TlsMode
            {
                CLIENT,
                SERVER,
            };

            class AWS_CRT_CPP_API TlsContextOptions final
            {
                friend class TlsContext;

              public:
                TlsContextOptions(const TlsContextOptions &) noexcept = default;
                TlsContextOptions &operator=(const TlsContextOptions &) noexcept = default;

                /**
                 * Initializes TlsContextOptions with secure by default options, with
                 * no client certificates.
                 */
                static TlsContextOptions InitDefaultClient() noexcept;
                /**
                 * Initializes TlsContextOptions with secure by default options, with
                 * client certificate and private key. These are paths to a file on disk. These
                 * strings must remain in memory for the lifetime of the returned object. These files
                 * must be in the PEM format.
                 */
                static TlsContextOptions InitClientWithMtls(const char *cert_path, const char *pkey_path) noexcept;

                /**
                 * Initializes TlsContextOptions with secure by default options, with
                 * client certificateand private key in the PKCS#12 format.
                 * This is a path to a file on disk. These
                 * strings must remain in memory for the lifetime of the returned object.
                 */
                static TlsContextOptions InitClientWithMtlsPkcs12(
                    const char *pkcs12_path,
                    const char *pkcs12_pwd) noexcept;

                /**
                 * Returns true if alpn is supported by the underlying security provider, false
                 * otherwise.
                 */
                static bool IsAlpnSupported() noexcept;

                /**
                 * Sets the list of alpn protocols, delimited by ';'. This string must remain in memory
                 * for the lifetime of this object.
                 */
                void SetAlpnList(const char *alpnList) noexcept;

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
                 * Overrides the default system trust store. caPath is only useful on Unix style systems where
                 * all anchors are stored in a directory (like /etc/ssl/certs). caFile is for a single file containing
                 * all trusted CAs. caFile must be in the PEM format.
                 *
                 * These strings must remain in memory for the lifetime of this object.
                 */
                void OverrideDefaultTrustStore(const char *caPath, const char *caFile) noexcept;

              private:
                aws_tls_ctx_options m_options;

                TlsContextOptions() noexcept;
            };

            class AWS_CRT_CPP_API TlsContext final
            {
              public:
                TlsContext(
                    TlsContextOptions &options,
                    TlsMode mode,
                    Allocator *allocator = DefaultAllocator()) noexcept;
                ~TlsContext();
                TlsContext(const TlsContext &) = delete;
                TlsContext &operator=(const TlsContext &) = delete;
                TlsContext(TlsContext &&) noexcept;
                TlsContext &operator=(TlsContext &&) noexcept;

                TlsConnectionOptions NewConnectionOptions() const noexcept;

                operator bool() const noexcept;
                int LastError() const noexcept;

              private:
                aws_tls_ctx *m_ctx;
                int m_lastError;
            };

            AWS_CRT_CPP_API void InitTlsStaticState(Allocator *alloc) noexcept;
            AWS_CRT_CPP_API void CleanUpTlsStaticState() noexcept;

        } // namespace Io
    }     // namespace Crt
} // namespace Aws
