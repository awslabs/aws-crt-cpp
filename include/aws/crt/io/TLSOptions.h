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

#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>

#include <aws/io/tls_channel_handler.h>

struct aws_tls_ctx_options;

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            using TLSCtxOptions = aws_tls_ctx_options;

            AWS_CRT_CPP_API void InitDefaultClient(TLSCtxOptions& options) noexcept;
            AWS_CRT_CPP_API void InitClientWithMTLS(TLSCtxOptions& options, 
                const char* cert_path, const char* pkey_path) noexcept;
            AWS_CRT_CPP_API void InitClientWithMTLSPkcs12(TLSCtxOptions& options, 
                const char* pkcs12_path, const char* pkcs12_pwd) noexcept;
            AWS_CRT_CPP_API void SetALPNList(TLSCtxOptions& options, const char* alpn_list) noexcept;
            AWS_CRT_CPP_API void SetVerifyPeer(TLSCtxOptions& options, bool verify_peer) noexcept;
            AWS_CRT_CPP_API void OverrideDefaultTrustStore(TLSCtxOptions& options, 
                const char* caPath, const char* caFile) noexcept;

            AWS_CRT_CPP_API void InitTLSStaticState(Allocator* alloc) noexcept;
            AWS_CRT_CPP_API void CleanUpTLSStaticState() noexcept;

            AWS_CRT_CPP_API bool IsALPNSupported() noexcept;
        }
    }
}