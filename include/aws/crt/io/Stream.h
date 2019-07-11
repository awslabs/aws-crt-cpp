#pragma once
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

#include <aws/crt/Exports.h>
#include <aws/crt/Types.h>

struct aws_input_stream;

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            /*
             * Factory to create a aws-c-io input stream subclass from a C++ stream
             */
            AWS_CRT_CPP_API aws_input_stream *AwsInputStreamNewCpp(
                const std::shared_ptr<Aws::Crt::Io::IStream> &stream,
                Aws::Crt::Allocator *allocator = DefaultAllocator()) noexcept;
        } // namespace Io
    }     // namespace Crt
} // namespace Aws
