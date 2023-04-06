/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/Types.h>
#include <aws/crt/io/HostResolver.h>
#include <aws/testing/aws_test_harness.h>

#include <condition_variable>
#include <mutex>

static int s_TestDefaultResolution(struct aws_allocator *allocator, void *)
{
    {
        Aws::Crt::ApiHandle apiHandle(allocator);

        Aws::Crt::Io::EventLoopGroup eventLoopGroup(0, allocator);
        ASSERT_TRUE(eventLoopGroup);
        ASSERT_NOT_NULL(eventLoopGroup.GetUnderlyingHandle());

        Aws::Crt::Io::DefaultHostResolver defaultHostResolver(eventLoopGroup, 8, 5, allocator);
        ASSERT_TRUE(defaultHostResolver);
        ASSERT_NOT_NULL(defaultHostResolver.GetUnderlyingHandle());

        std::condition_variable semaphore;
        std::mutex semaphoreLock;
        size_t addressCount = 0;
        int error = 0;

        auto onHostResolved = [&](Aws::Crt::Io::HostResolver &,
                                  const Aws::Crt::Vector<Aws::Crt::Io::HostAddress> &addresses,
                                  int errorCode)
        {
            {
                std::lock_guard<std::mutex> lock(semaphoreLock);
                addressCount = addresses.size();
                error = errorCode;
            }
            semaphore.notify_one();
        };

        ASSERT_TRUE(defaultHostResolver.ResolveHost("localhost", onHostResolved));
        {
            std::unique_lock<std::mutex> lock(semaphoreLock);
            semaphore.wait(lock, [&]() { return addressCount || error; });
        }
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(DefaultResolution, s_TestDefaultResolution)

class TestCustomResolver : public Aws::Crt::Io::CustomHostResolverBase
{
  public:
    TestCustomResolver(Aws::Crt::Allocator *allocator) : Aws::Crt::Io::CustomHostResolverBase(allocator) {}

    Aws::Crt::String requestedResolvehost;
    size_t failedCount{};
    bool purgeCacheCalled{};

  protected:
    int OnResolveHost(const Aws::Crt::String &host, const Aws::Crt::Io::OnHostResolved &onResolved) noexcept override
    {
        requestedResolvehost = host;
        Aws::Crt::Vector<Aws::Crt::Io::HostAddress> addressList;
        Aws::Crt::Io::HostAddress localHostAddr;
        localHostAddr.address = aws_string_new_from_c_str(m_allocator, "127.0.0.1");
        localHostAddr.allocator = m_allocator;
        localHostAddr.host = aws_string_new_from_c_str(m_allocator, host.c_str());
        localHostAddr.record_type = AWS_ADDRESS_RECORD_TYPE_A;
        addressList.push_back(localHostAddr);
        onResolved(*this, addressList, AWS_OP_SUCCESS);
        aws_host_address_clean_up(&localHostAddr);
        return AWS_OP_SUCCESS;
    }

    int OnRecordConnectionFailure(const Aws::Crt::Io::HostAddress &) noexcept override
    {
        failedCount++;
        return AWS_OP_SUCCESS;
    }

    int OnPurgeCache() noexcept override
    {
        purgeCacheCalled = true;
        return AWS_OP_SUCCESS;
    }

    size_t GetHostAddressCount(const Aws::Crt::String &, uint32_t) noexcept { return 42; }
};

static void s_onTestHostResolved(
    struct aws_host_resolver *,
    const struct aws_string *hostName,
    int,
    const struct aws_array_list *,
    void *userData)
{
    auto *resolverWrapper = reinterpret_cast<TestCustomResolver *>(userData);
    resolverWrapper->requestedResolvehost = Aws::Crt::String(aws_string_c_str(hostName), hostName->len);
}

/* Uses the shims above to make sure the custom resolution wiring is correctly hooked up and not leaking. */
static int s_TestCustomResolution(struct aws_allocator *allocator, void *)
{
    {
        Aws::Crt::ApiHandle apiHandle(allocator);

        TestCustomResolver testCustomResolver(allocator);
        ASSERT_TRUE(testCustomResolver);
        ASSERT_NOT_NULL(testCustomResolver.GetUnderlyingHandle());

        std::condition_variable semaphore;
        std::mutex semaphoreLock;
        size_t addressCount = 0;
        int error = 0;

        auto onHostResolved = [&](Aws::Crt::Io::HostResolver &,
                                  const Aws::Crt::Vector<Aws::Crt::Io::HostAddress> &addresses,
                                  int errorCode)
        {
            addressCount = addresses.size();
            error = errorCode;
        };

        ASSERT_TRUE(testCustomResolver.ResolveHost("localhost", onHostResolved));
        ASSERT_STR_EQUALS("localhost", testCustomResolver.requestedResolvehost.c_str());

        ASSERT_SUCCESS(aws_host_resolver_purge_cache(testCustomResolver.GetUnderlyingHandle()));
        ASSERT_TRUE(testCustomResolver.purgeCacheCalled);

        Aws::Crt::Io::HostAddress localHostAddr;
        ASSERT_SUCCESS(
            aws_host_resolver_record_connection_failure(testCustomResolver.GetUnderlyingHandle(), &localHostAddr));
        ASSERT_SUCCESS(
            aws_host_resolver_record_connection_failure(testCustomResolver.GetUnderlyingHandle(), &localHostAddr));
        ASSERT_UINT_EQUALS(2, testCustomResolver.failedCount);

        aws_string *hostName = aws_string_new_from_c_str(allocator, "localhost");
        ASSERT_UINT_EQUALS(
            42,
            aws_host_resolver_get_host_address_count(
                testCustomResolver.GetUnderlyingHandle(),
                hostName,
                AWS_ADDRESS_RECORD_TYPE_A | AWS_ADDRESS_RECORD_TYPE_AAAA));

        testCustomResolver.requestedResolvehost.clear();
        aws_host_resolver_resolve_host(
            testCustomResolver.GetUnderlyingHandle(),
            hostName,
            s_onTestHostResolved,
            testCustomResolver.GetConfig(),
            reinterpret_cast<void *>(&testCustomResolver));
        ASSERT_STR_EQUALS("localhost", testCustomResolver.requestedResolvehost.c_str());

        aws_string_destroy(hostName);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(CustomResolution, s_TestCustomResolution)