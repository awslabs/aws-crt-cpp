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
#include <aws/crt/DateTime.h>
#include <aws/crt/auth/Sigv4Signing.h>
#include <aws/crt/http/HttpConnectionManager.h>

using TransportOpCompleted = std::function<void(int errorCode)>;

class S3ObjectTransport
{
  public:
    S3ObjectTransport(
        const Aws::Crt::String &region,
        const Aws::Crt::String &bucket,
        Aws::Crt::Io::TlsContext &tlsContext,
        Aws::Crt::Io::ClientBootstrap &clientBootstrap,
        const std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> &credsProvider,
        const std::shared_ptr<Aws::Crt::Auth::Sigv4HttpRequestSigner> &signer,
        size_t maxCons = 1000);

    void PutObject(
        const Aws::Crt::String &key,
        struct aws_input_stream *inputStream,
        TransportOpCompleted transportOpCompleted);
    void GetObject(
        const Aws::Crt::String &key,
        Aws::Crt::Http::OnIncomingBody onIncomingBody,
        TransportOpCompleted transportOpCompleted);

  private:
    std::shared_ptr<Aws::Crt::Http::HttpClientConnectionManager> m_connManager;
    std::shared_ptr<Aws::Crt::Auth::Sigv4HttpRequestSigner> m_signer;
    std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> m_credsProvider;
    const Aws::Crt::String m_region;
    const Aws::Crt::String m_bucketName;
    Aws::Crt::Http::HttpHeader m_hostHeader;
    Aws::Crt::Http::HttpHeader m_contentTypeHeader;
    Aws::Crt::String m_endpoint;
};