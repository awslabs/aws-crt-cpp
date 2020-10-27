/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/iot/SecureTunnel.h>
#include <aws/http/http.h>
#include <aws/testing/aws_test_harness.h>

struct SecureTunnelingTestContext
{
    aws_secure_tunneling_local_proxy_mode localProxyMode;
    Aws::Crt::Iot::SecureTunnel *secureTunnel;

    SecureTunnelingTestContext()
    {
        localProxyMode = AWS_SECURE_TUNNELING_DESTINATION_MODE;
        secureTunnel = nullptr;
    }
};
static SecureTunnelingTestContext s_testContext;

// Client callbacks implementation
static void s_OnConnectionComplete() {}

static void s_OnSendDataComplete(int errorCode) {}

static void s_OnDataReceive(const Aws::Crt::ByteBuf &data) {}

static void s_OnStreamStart() {}

static void s_OnStreamReset() {}

static void s_OnSessionReset() {}

static int before(struct aws_allocator *allocator, void *ctx)
{
    auto *testContext = static_cast<SecureTunnelingTestContext *>(ctx);

    aws_http_library_init(allocator);
    aws_iotdevice_library_init(allocator);

    testContext->secureTunnel = new Aws::Crt::Iot::SecureTunnel(
        allocator,
        nullptr,
        nullptr,
        "access_token",
        testContext->localProxyMode,
        "endpoint",
        s_OnConnectionComplete,
        s_OnSendDataComplete,
        s_OnDataReceive,
        s_OnStreamStart,
        s_OnStreamReset,
        s_OnSessionReset);

    return AWS_ERROR_SUCCESS;
}

static int after(struct aws_allocator *allocator, int setup_result, void *ctx)
{
    auto *testContext = static_cast<SecureTunnelingTestContext *>(ctx);

    delete testContext->secureTunnel;
    testContext->secureTunnel = nullptr;

    aws_iotdevice_library_clean_up();
    aws_http_library_clean_up();

    return AWS_ERROR_SUCCESS;
}

AWS_TEST_CASE_FIXTURE(SecureTunnelTest1, before, s_SecureTunnelTest1, after, &s_testContext);
static int s_SecureTunnelTest1(Aws::Crt::Allocator *allocator, void *ctx)
{
    return AWS_ERROR_SUCCESS;
}
