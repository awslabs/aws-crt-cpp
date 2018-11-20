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
#include <aws/crt/Api.h>

#include <aws/crt/io/TlsOptions.h>

namespace Aws
{
    namespace Crt
    {
        static void s_initApi(Allocator* allocator)
        {
            Io::InitTlsStaticState(allocator);
        }

        static void s_cleanUpApi()
        {
            Io::CleanUpTlsStaticState();
        }

        ApiHandle::ApiHandle(Allocator* allocator) noexcept
        {
            s_initApi(allocator);
        }

        ApiHandle::ApiHandle() noexcept
        {
            s_initApi(DefaultAllocator());
        }

        ApiHandle::~ApiHandle()
        {
            s_cleanUpApi();
        }

        void LoadErrorStrings() noexcept
        {
            aws_load_error_strings();
            aws_io_load_error_strings();
            aws_mqtt_load_error_strings();
        }

        const char* ErrorDebugString(int error) noexcept
        {
            return aws_error_debug_str(error);
        }
    }
}
