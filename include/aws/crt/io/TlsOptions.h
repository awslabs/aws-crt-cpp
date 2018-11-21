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

            enum class TLSMode
            {
                CLIENT,
                SERVER,
            };

            class AWS_CRT_CPP_API TlsContextOptions final
            {
                friend class TlsContext;
            public:
                TlsContextOptions(const TlsContextOptions&) noexcept = default;
                TlsContextOptions& operator=(const TlsContextOptions&) noexcept = default;

                static TlsContextOptions InitDefaultClient() noexcept;
                static TlsContextOptions InitClientWithMtls(const char *cert_path, const char *pkey_path) noexcept;
                static TlsContextOptions InitClientWithMtlsPkcs12(const char *pkcs12_path,
                        const char *pkcs12_pwd) noexcept;
                static bool IsAlpnSupported() noexcept;

                void SetAlpnList(const char* alpnList) noexcept;
                void SetVerifyPeer(bool verifyPeer) noexcept;
                void OverrideDefaultTrustStore(const char* caPath, const char*caFile) noexcept;

            private:
                aws_tls_ctx_options m_options;

                TlsContextOptions() noexcept;
            };

            class AWS_CRT_CPP_API TlsContext final
            {
            public:
                TlsContext(TlsContextOptions& options, TLSMode mode, Allocator* allocator = DefaultAllocator()) noexcept;
                ~TlsContext();
                TlsContext(const TlsContext&) = delete;
                TlsContext& operator=(const TlsContext&) = delete;
                TlsContext(TlsContext&&) noexcept;
                TlsContext& operator=(TlsContext&&) noexcept;

                TlsConnectionOptions NewConnectionOptions() const noexcept;

                operator bool() const noexcept;
                int LastError() const noexcept;

            private:
                aws_tls_ctx* m_ctx;
                int m_lastError;
            };

            AWS_CRT_CPP_API void InitTlsStaticState(Allocator *alloc) noexcept;
            AWS_CRT_CPP_API void CleanUpTlsStaticState() noexcept;

        }
    }
}
