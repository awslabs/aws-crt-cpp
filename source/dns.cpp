/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/dns.h>
#include <aws/common/host_utils.h>
#include <aws/crt/Types.h>

namespace Aws {
namespace Crt {
namespace dns {

bool IsValidIpV6(const char* host, bool is_uri_encoded) {
    return aws_host_utils_is_ipv6(Aws::Crt::ByteCursorFromCString(host), is_uri_encoded);
}

}  // namespace dns
}  // namespace Crt
}  // namespace Aws