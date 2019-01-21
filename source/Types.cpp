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

#include <aws/common/encoding.h>

namespace Aws
{
    namespace Crt
    {
        Allocator *DefaultAllocator() noexcept { return aws_default_allocator(); }

        ByteBuf ByteBufFromCString(const char *str) noexcept { return aws_byte_buf_from_c_str(str); }

        ByteBuf ByteBufFromArray(const uint8_t *array, size_t len) noexcept
        {
            return aws_byte_buf_from_array(array, len);
        }

        ByteBuf ByteBufNewCopy(Allocator *alloc, const uint8_t *array, size_t len)
        {
            ByteBuf retVal;
            ByteBuf src = aws_byte_buf_from_array(array, len);
            aws_byte_buf_init_copy(&retVal, alloc, &src);
            return retVal;
        }

        void ByteBufDelete(ByteBuf &buf) { aws_byte_buf_clean_up(&buf); }

        Vector<uint8_t> Base64Decode(const String &decode)
        {
            ByteCursor toDecode = aws_byte_cursor_from_array((const void *)decode.data(), decode.length());

            size_t allocation_size = 0;

            if (aws_base64_compute_decoded_len(&toDecode, &allocation_size) == AWS_OP_SUCCESS)
            {
                Vector<uint8_t> output(allocation_size, 0x00);
                ByteBuf tempBuf = aws_byte_buf_from_array(output.data(), output.size());
                tempBuf.len = 0;

                if (aws_base64_decode(&toDecode, &tempBuf))
                {
                    return output;
                }
            }

            return {};
        }

        String Base64Encode(const Vector<uint8_t> &encode)
        {
            ByteCursor toEncode = aws_byte_cursor_from_array((const void *)encode.data(), encode.size());

            size_t allocation_size = 0;

            if (aws_base64_compute_encoded_len(encode.size(), &allocation_size) == AWS_OP_SUCCESS)
            {
                String output(allocation_size, 0x00);
                ByteBuf tempBuf = aws_byte_buf_from_array(output.data(), output.size());
                tempBuf.len = 0;

                if (aws_base64_encode(&toEncode, &tempBuf))
                {
                    return output;
                }
            }

            return {};
        }

    } // namespace Crt
} // namespace Aws
