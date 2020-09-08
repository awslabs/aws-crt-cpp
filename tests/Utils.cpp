/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>
#include <aws/crt/StlAllocator.h>

#include <aws/auth/auth.h>
#include <aws/common/ref_count.h>
#include <aws/http/http.h>
#include <aws/mqtt/mqtt.h>

namespace Aws
{
    namespace Crt
    {
        void TestCleanupAndWait()
        {
            aws_global_thread_creator_shutdown_wait_for(5);

            g_allocator = nullptr;
            aws_auth_library_clean_up();
            aws_mqtt_library_clean_up();
            aws_http_library_clean_up();
        }
    } // namespace Crt
} // namespace Aws
