#pragma once


#include <aws/io/event_loop.h>
#include <aws/io/host_resolver.h>

#include <condition_variable>
#include <mutex>
#include <vector>

#include <inttypes.h>

class CustomHostResolver {
public:

  CustomHostResolver();

  static int resolveHost(
      struct aws_allocator *allocator,
      const struct aws_string *host_name,
      struct aws_array_list *output_addresses,
      void *user_data);

  void setSeedCount(uint32_t count) { seedCount = count; }

private:

  static void clientBootstrapShutdownComplete(void *user_data);
  static void onResolverDestroyed(void *user_data);
  static void onQueryComplete(struct aws_dns_query_result *result, int error_code, void *user_data);

  int resolveHostInternal(struct aws_allocator *allocator,
                  const struct aws_string *host_name,
                  struct aws_array_list *output_addresses);

  void initResolvers(struct aws_allocator *allocator);
  void makeQueries(const struct aws_string *host_name);
  void waitForAnswers();
  void cleanupResolvers();
  void seedHosts(struct aws_allocator *allocator,
            const struct aws_string *host_name,
            struct aws_array_list *output_addresses);

  std::mutex lock;
  std::condition_variable signal;
  bool seeded;
  uint32_t seedCount;

  uint64_t seedStartTimeNs;

  struct aws_allocator *alloc;
  struct aws_event_loop_group elGroup;
  struct aws_host_resolver oldResolver;
  struct aws_client_bootstrap *bootstrap;
  std::vector<struct aws_dns_resolver_udp_channel *> resolvers;

  struct aws_array_list *seededAddresses;
  const struct aws_string *hostToLookup;

  uint32_t queryCount;
  uint32_t answerCount;
  uint32_t resolverShutdownsRemaining;
  bool bootstrapReleased;

};
