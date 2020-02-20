#include "CustomHostResolver.h"

#include <aws/io/channel_bootstrap.h>
#include <aws/io/dns_impl.h>
#include "aws/io/host_resolver.h"
#include <aws/common/string.h>


CustomHostResolver::CustomHostResolver() :
    lock(),
    signal(),
    seeded(false),
    seedCount(0),
    seedStartTimeNs(0),
    elGroup(),
    oldResolver(),
    bootstrap(nullptr),
    resolvers(),
    seededAddresses(nullptr),
    answerCount(0),
    resolverShutdownsRemaining(0),
    bootstrapReleased(false)
{
}

int CustomHostResolver::resolveHost(
    struct aws_allocator *allocator,
    const struct aws_string *host_name,
    struct aws_array_list *output_addresses,
    void *user_data) {

    CustomHostResolver *resolver = static_cast<CustomHostResolver *>(user_data);
    return resolver->resolveHostInternal(allocator, host_name, output_addresses);
}

int CustomHostResolver::resolveHostInternal(struct aws_allocator *allocator,
                const struct aws_string *host_name,
                struct aws_array_list *output_addresses) {

    bool should_seed = false;
    if (strcmp((const char *)(host_name->bytes), "aws-crt-canary-bucket.s3.us-west-2.amazonaws.com") == 0){
        std::lock_guard<std::mutex> stateLock(lock);
        if (seeded == false && seedCount > 0) {
            should_seed = true;
            seeded = true;
        }
    }

    if (should_seed) {
        seedHosts(allocator, host_name, output_addresses);
        return AWS_OP_SUCCESS;
    } else {
        return aws_default_dns_resolve(allocator, host_name, output_addresses, nullptr);
    }
}

void CustomHostResolver::clientBootstrapShutdownComplete(void *user_data) {
    CustomHostResolver *resolver = static_cast<CustomHostResolver *>(user_data);

    {
        std::lock_guard<std::mutex> guard(resolver->lock);
        resolver->bootstrapReleased = true;
    }

    resolver->signal.notify_one();
}

void CustomHostResolver::onResolverDestroyed(void *user_data) {
    CustomHostResolver *resolver = static_cast<CustomHostResolver *>(user_data);

    {
        std::lock_guard<std::mutex> guard(resolver->lock);
        --resolver->resolverShutdownsRemaining;
    }

    resolver->signal.notify_one();
}

void CustomHostResolver::initResolvers(struct aws_allocator *allocator) {
    aws_event_loop_group_default_init(&elGroup, allocator, 1);
    aws_host_resolver_init_default(&oldResolver, allocator, 16, &elGroup);

    struct aws_client_bootstrap_options bootstrap_options;
    AWS_ZERO_STRUCT(bootstrap_options);
    bootstrap_options.event_loop_group = &elGroup;
    bootstrap_options.host_resolver = &oldResolver;
    bootstrap_options.on_shutdown_complete = clientBootstrapShutdownComplete;
    bootstrap_options.user_data = this;

    bootstrap = aws_client_bootstrap_new(allocator, &bootstrap_options);

    struct aws_dns_resolver_udp_channel_options resolver_options;
    AWS_ZERO_STRUCT(resolver_options);
    resolver_options.bootstrap = bootstrap;
    resolver_options.host = aws_byte_cursor_from_c_str("205.251.194.41");
    resolver_options.port = 53;
    resolver_options.on_destroyed_callback = onResolverDestroyed;
    resolver_options.on_destroyed_user_data = this;

    struct aws_dns_resolver_udp_channel *resolver = aws_dns_resolver_udp_channel_new(allocator, &resolver_options);
    resolvers.push_back(resolver);

    resolverShutdownsRemaining = 1;

    this->allocator = allocator;
}

void CustomHostResolver::onQueryComplete(struct aws_dns_query_result *result, int error_code, void *user_data) {
    (void)result;
    (void)error_code;
    CustomHostResolver *resolver = static_cast<CustomHostResolver *>(user_data);

    if (result != nullptr)
    {
        std::lock_guard<std::mutex> guard(resolver->lock);

        size_t answer_records = aws_array_list_length(&result->answer_records);
        for (size_t i = 0; i < answer_records; ++i) {
            struct aws_dns_resource_record *record = NULL;
            aws_array_list_get_at_ptr(&result->answer_records, (void **) &record, i);

            if (record->type != AWS_DNS_RR_A) {
                continue;
            }

            struct aws_host_address host_address;
            AWS_ZERO_STRUCT(host_address);

            char buffer[256];
            sprintf(buffer, "%d.%d.%d.%d", (int) record->data.buffer[0], (int) record->data.buffer[1],
                    (int) record->data.buffer[2], (int) record->data.buffer[3]);

            host_address.record_type = AWS_ADDRESS_RECORD_TYPE_A;
            host_address.address = aws_string_new_from_c_str(resolver->allocator, buffer);
            host_address.weight = 0;
            host_address.allocator = resolver->allocator;
            host_address.use_count = 0;
            host_address.connection_failure_count = 0;
            host_address.host = aws_string_new_from_string(resolver->allocator, resolver->hostToLookup);

            {
                ++resolver->answerCount;

                aws_array_list_push_back(resolver->seededAddresses, &host_address);
            }
        }
    }

    resolver->signal.notify_one();

    return;
}

void CustomHostResolver::makeQueries(const struct aws_string *host_name) {

    (void)host_name;

    {
        std::lock_guard<std::mutex> guard(lock);
        queryCount = seedCount;

        struct aws_dns_query query;
        AWS_ZERO_STRUCT(query);
        query.query_type = AWS_DNS_RR_A;
        query.hostname = aws_byte_cursor_from_c_str("s3-r-w.us-west-2.amazonaws.com");
        query.on_completed_callback = onQueryComplete;
        query.user_data = this;

        for (size_t i = 0; i < seedCount; ++i) {
            aws_dns_resolver_udp_channel_make_query(resolvers[0], &query);
        }
    }
}

void CustomHostResolver::waitForAnswers() {
    {
        std::unique_lock<std::mutex> guard(lock);
        signal.wait(guard, [this]() { return answerCount >= queryCount; });
    }
}

void CustomHostResolver::cleanupResolvers() {
    for (auto resolver : resolvers) {
        aws_dns_resolver_udp_channel_destroy(resolver);
    }

    {
        std::unique_lock<std::mutex> guard(lock);
        signal.wait(guard, [this]() { return resolverShutdownsRemaining == 0; });
    }

    aws_client_bootstrap_release(bootstrap);

    {
        std::unique_lock<std::mutex> guard(lock);
        signal.wait(guard, [this]() { return bootstrapReleased == true; });
    }

    aws_host_resolver_clean_up(&oldResolver);
    aws_event_loop_group_clean_up(&elGroup);
}

void CustomHostResolver::seedHosts(struct aws_allocator *allocator,
                                  const struct aws_string *host_name,
                                  struct aws_array_list *output_addresses) {

    seededAddresses = output_addresses;
    hostToLookup = host_name;

    initResolvers(allocator);

    makeQueries(host_name);

    waitForAnswers();

    cleanupResolvers();
}