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
#include <aws/crt/Types.h>

#include <aws/crt/ByteBuf.h>

#include <aws/common/encoding.h>

namespace Aws
{
    namespace Crt
    {
        Allocator *DefaultAllocator() noexcept { return aws_default_allocator(); }

        Vector<uint8_t> Base64Decode(const String &decode)
        {
            ByteCursor toDecode(reinterpret_cast<const uint8_t *>(decode.data()), decode.length());

            size_t allocation_size = 0;

            if (aws_base64_compute_decoded_len(toDecode.Get(), &allocation_size) == AWS_OP_SUCCESS)
            {
                Vector<uint8_t> output(allocation_size, 0x00);
                ByteBuf tempBuf(output.data(), output.size(), 0);

                if (aws_base64_decode(toDecode.Get(), tempBuf.Get()) == AWS_OP_SUCCESS)
                {
                    return output;
                }
            }

            return {};
        }

        String Base64Encode(const Vector<uint8_t> &encode)
        {
            ByteCursor toEncode(reinterpret_cast<const uint8_t *>(encode.data()), encode.size());

            size_t allocation_size = 0;

            if (aws_base64_compute_encoded_len(encode.size(), &allocation_size) == AWS_OP_SUCCESS)
            {
                String output(allocation_size, 0x00);
                ByteBuf tempBuf(reinterpret_cast<const uint8_t *>(output.data()), output.size(), 0);

                if (aws_base64_encode(toEncode.Get(), tempBuf.Get()))
                {
                    return output;
                }
            }

            return {};
        }

    } // namespace Crt
} // namespace Aws
