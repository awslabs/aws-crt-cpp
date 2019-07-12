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
#include <aws/crt/Api.h>

#include <aws/crt/auth/Credentials.h>
#include <aws/crt/auth/Sigv4Signing.h>

#include <aws/testing/aws_test_harness.h>

using namespace Aws::Crt::Auth;

static int s_Sigv4SignerTestCreateDestroy(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        auto signer = Aws::Crt::MakeShared<Sigv4HttpRequestSigner>(allocator, allocator);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(Sigv4SignerTestCreateDestroy, s_Sigv4SignerTestCreateDestroy)

static int s_Sigv4SignerTestSimple(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(Sigv4SignerTestSimple, s_Sigv4SignerTestSimple)

static int s_Sigv4SigningPipelineTestCreateDestroy(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
        ASSERT_TRUE(eventLoopGroup);

        Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 30, allocator);
        ASSERT_TRUE(defaultHostResolver);

        Aws::Crt::Io::ClientBootstrap clientBootstrap(eventLoopGroup, defaultHostResolver, allocator);
        ASSERT_TRUE(clientBootstrap);

        CredentialsProviderChainDefaultConfig config;
        config.m_bootstrap = &clientBootstrap;

        auto provider = Aws::Crt::Auth::CredentialsProvider::CreateCredentialsProviderChainDefault(config);

        auto pipeline = Aws::Crt::MakeShared<Sigv4HttpRequestSigningPipeline>(allocator, provider, allocator);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(Sigv4SigningPipelineTestCreateDestroy, s_Sigv4SigningPipelineTestCreateDestroy)

static int s_Sigv4SigningPipelineTestSimple(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(Sigv4SigningPipelineTestSimple, s_Sigv4SigningPipelineTestSimple)