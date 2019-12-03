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
#include "S3ObjectTransport.h"

#include <aws/crt/http/HttpConnectionManager.h>
#include <aws/crt/http/HttpRequestResponse.h>
#include <aws/io/stream.h>

using namespace Aws::Crt;

S3ObjectTransport::S3ObjectTransport(
    const Aws::Crt::String &region,
    const Aws::Crt::String &bucket,
    Aws::Crt::Io::TlsContext &tlsContext,
    Aws::Crt::Io::ClientBootstrap &clientBootstrap,
    const std::shared_ptr<Aws::Crt::Auth::ICredentialsProvider> &credsProvider,
    const std::shared_ptr<Aws::Crt::Auth::Sigv4HttpRequestSigner> &signer,
    size_t maxCons)
    : m_signer(signer), m_credsProvider(credsProvider), m_region(region), m_bucketName(bucket)
{
    Http::HttpClientConnectionManagerOptions connectionManagerOptions;
    m_endpoint = m_bucketName + ".s3." + m_region + ".amazonaws.com";
    connectionManagerOptions.ConnectionOptions.HostName = m_endpoint;
    connectionManagerOptions.ConnectionOptions.Port = 443;
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetConnectTimeoutMs(3000);
    connectionManagerOptions.ConnectionOptions.SocketOptions.SetSocketType(AWS_SOCKET_STREAM);
    connectionManagerOptions.ConnectionOptions.InitialWindowSize = SIZE_MAX;

    aws_byte_cursor serverName = ByteCursorFromCString(m_endpoint.c_str());

    auto connOptions = tlsContext.NewConnectionOptions();
    connOptions.SetServerName(serverName);
    connectionManagerOptions.ConnectionOptions.TlsOptions = connOptions;
    connectionManagerOptions.ConnectionOptions.Bootstrap = &clientBootstrap;
    connectionManagerOptions.MaxConnections = maxCons;

    m_connManager =
        Http::HttpClientConnectionManager::NewClientConnectionManager(connectionManagerOptions, g_allocator);

    m_hostHeader.name = ByteCursorFromCString("host");
    m_hostHeader.value = ByteCursorFromCString(m_endpoint.c_str());

    m_contentTypeHeader.name = ByteCursorFromCString("content-type");
    m_contentTypeHeader.value = ByteCursorFromCString("text/plain");
}

void S3ObjectTransport::PutObject(
    const Aws::Crt::String &key,
    struct aws_input_stream *inputStream,
    TransportOpCompleted transportOpCompleted)
{
    auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
    request->AddHeader(m_hostHeader);
    request->AddHeader(m_contentTypeHeader);

    Http::HttpHeader contentLength;
    contentLength.name = ByteCursorFromCString("content-length");

    int64_t streamLen = 0;

    aws_input_stream_get_length(inputStream, &streamLen);
    StringStream intValue;
    intValue << streamLen;
    String contentLengthVal = intValue.str();
    contentLength.value = ByteCursorFromCString(contentLengthVal.c_str());
    request->AddHeader(contentLength);

    aws_http_message_set_body_stream(request->GetUnderlyingMessage(), inputStream);
    request->SetMethod(aws_http_method_put);

    String keyPath = "/" + key;
    ByteCursor path = ByteCursorFromCString(keyPath.c_str());
    request->SetPath(path);

    Auth::AwsSigningConfig signingConfig(g_allocator);
    signingConfig.SetRegion(m_region);
    signingConfig.SetCredentialsProvider(m_credsProvider);
    signingConfig.SetService("s3");
    signingConfig.SetBodySigningType(Auth::BodySigningType::UnsignedPayload);
    signingConfig.SetSigningTimepoint(DateTime::Now());
    signingConfig.SetSigningAlgorithm(Auth::SigningAlgorithm::SigV4Header);

    m_signer->SignRequest(
        request,
        signingConfig,
        [this,
         transportOpCompleted](const std::shared_ptr<Aws::Crt::Http::HttpRequest> &signedRequest, int signingError) {
            if (signingError == AWS_OP_SUCCESS)
            {
                m_connManager->AcquireConnection([signedRequest, transportOpCompleted](
                                                     std::shared_ptr<Http::HttpClientConnection> conn, int connError) {
                    if (connError == AWS_OP_SUCCESS)
                    {
                        Http::HttpRequestOptions requestOptions;
                        AWS_ZERO_STRUCT(requestOptions);
                        requestOptions.request = signedRequest.get();
                        requestOptions.onStreamComplete =
                            [signedRequest, conn, transportOpCompleted](Http::HttpStream &stream, int error) {
                                int errorCode = error;

                                if (!errorCode)
                                {
                                    errorCode = stream.GetResponseStatusCode() == 200 ? AWS_OP_SUCCESS : AWS_OP_ERR;
                                }

                                transportOpCompleted(errorCode);
                            };
                        conn->NewClientStream(requestOptions);
                    }
                    else
                    {
                        transportOpCompleted(connError);
                    }
                });
            }
            else
            {
                transportOpCompleted(signingError);
            }
        });
}

void S3ObjectTransport::GetObject(
    const Aws::Crt::String &key,
    Aws::Crt::Http::OnIncomingBody onIncomingBody,
    TransportOpCompleted transportOpCompleted)
{
    auto request = MakeShared<Http::HttpRequest>(g_allocator, g_allocator);
    request->AddHeader(m_hostHeader);

    request->SetMethod(aws_http_method_get);

    String keyPath = "/" + key;
    ByteCursor path = ByteCursorFromCString(keyPath.c_str());
    request->SetPath(path);

    Auth::AwsSigningConfig signingConfig(g_allocator);
    signingConfig.SetRegion(m_region);
    signingConfig.SetCredentialsProvider(m_credsProvider);
    signingConfig.SetService("s3");
    signingConfig.SetBodySigningType(Auth::BodySigningType::UnsignedPayload);
    signingConfig.SetSigningTimepoint(DateTime::Now());
    signingConfig.SetSigningAlgorithm(Auth::SigningAlgorithm::SigV4Header);

    m_signer->SignRequest(
        request,
        signingConfig,
        [this, onIncomingBody, transportOpCompleted](
            const std::shared_ptr<Aws::Crt::Http::HttpRequest> &signedRequest, int signingError) {
            if (signingError == AWS_OP_SUCCESS)
            {
                m_connManager->AcquireConnection([signedRequest, onIncomingBody, transportOpCompleted](
                                                     std::shared_ptr<Http::HttpClientConnection> conn, int connError) {
                    if (connError == AWS_OP_SUCCESS)
                    {
                        Http::HttpRequestOptions requestOptions;
                        AWS_ZERO_STRUCT(requestOptions);
                        requestOptions.request = signedRequest.get();
                        requestOptions.onIncomingBody = onIncomingBody;
                        requestOptions.onStreamComplete =
                            [signedRequest, conn, transportOpCompleted](Http::HttpStream &stream, int error) {
                                int errorCode = error;

                                if (!errorCode)
                                {
                                    errorCode = stream.GetResponseStatusCode() == 200 ? AWS_OP_SUCCESS : AWS_OP_ERR;
                                }

                                transportOpCompleted(errorCode);
                            };
                        conn->NewClientStream(requestOptions);
                    }
                    else
                    {
                        transportOpCompleted(connError);
                    }
                });
            }
            else
            {
                transportOpCompleted(signingError);
            }
        });
}