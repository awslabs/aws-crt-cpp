/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Types.h>

#include <aws/common/encoding.h>

namespace Aws
{
    namespace Crt
    {
        ByteBuf ByteBufFromCString(const char *str) noexcept { return aws_byte_buf_from_c_str(str); }

        ByteBuf ByteBufFromEmptyArray(const uint8_t *array, size_t len) noexcept
        {
            return aws_byte_buf_from_empty_array(array, len);
        }

        ByteBuf ByteBufFromArray(const uint8_t *array, size_t capacity) noexcept
        {
            return aws_byte_buf_from_array(array, capacity);
        }

        ByteBuf ByteBufNewCopy(Allocator *alloc, const uint8_t *array, size_t len)
        {
            ByteBuf retVal;
            ByteBuf src = aws_byte_buf_from_array(array, len);
            aws_byte_buf_init_copy(&retVal, alloc, &src);
            return retVal;
        }

        void ByteBufDelete(ByteBuf &buf) { aws_byte_buf_clean_up(&buf); }

        ByteCursor ByteCursorFromCString(const char *str) noexcept { return aws_byte_cursor_from_c_str(str); }

        ByteCursor ByteCursorFromString(const Crt::String &str) noexcept
        {
            return aws_byte_cursor_from_array((const void *)str.data(), str.length());
        }

        ByteCursor ByteCursorFromStringView(const Crt::StringView &str) noexcept
        {
            return aws_byte_cursor_from_array((const void *)str.data(), str.length());
        }

        ByteCursor ByteCursorFromByteBuf(const ByteBuf &buf) noexcept { return aws_byte_cursor_from_buf(&buf); }

        ByteCursor ByteCursorFromArray(const uint8_t *array, size_t len) noexcept
        {
            return aws_byte_cursor_from_array(array, len);
        }

        Vector<uint8_t> Base64Decode(const String &decode) noexcept
        {
            ByteCursor toDecode = ByteCursorFromString(decode);

            size_t allocationSize = 0;

            if (aws_base64_compute_decoded_len(&toDecode, &allocationSize) == AWS_OP_SUCCESS)
            {
                Vector<uint8_t> output(allocationSize, 0x00);
                ByteBuf tempBuf = aws_byte_buf_from_empty_array(output.data(), output.capacity());
                
                ::Base64Decode(toDecode, tempBuf);

                if (tempBuf.len == allocationSize)
                {
                    return output;
                }
            }

            return {};
        }

        String Base64Encode(const Vector<uint8_t> &encode) noexcept
        {
            auto toEncode = aws_byte_cursor_from_array((const void *)encode.data(), encode.size());

            size_t allocationSize = 0;
            if (aws_base64_compute_encoded_len(toEncode.len, &allocationSize) == AWS_OP_SUCCESS)
            {
                String outputStr(allocationSize, 0x00);
                auto tempBuf = aws_byte_buf_from_empty_array(outputStr.data(), outputStr.capacity());
                UnsafeInteropHelpers::Base64Encode(toEncode, tempBuf);

                // encoding appends a null terminator, and accounts for it in the encoded length,
                // which makes the string 1 character too long
                if (tempBuf.len == allocationSize - 1)
                {
                    outputStr.pop_back();
                    return outputStr;
                }
            }

            return {};
        }

        namespace UnsafeInteropHelpers
        {
            void Base64Decode(const ByteCursor &toDecode, ByteBuf &out) noexcept
            {
                size_t allocation_size = 0;
                if (aws_base64_compute_decoded_len(&toDecode, &allocation_size) == AWS_OP_SUCCESS)
                {
                    if (out.capacity - out.len < allocation_size)
                    {
                        return;
                    }

                    aws_base64_decode(&toDecode, &out);
                }
            }

            void Base64Encode(const ByteCursor &toEncode, ByteBuf &output) noexcept
            {
                size_t allocation_size = 0;

                if (aws_base64_compute_encoded_len(toEncode.len, &allocation_size) == AWS_OP_SUCCESS)
                {
                    if (output.capacity - output.len < allocation_size)
                    {
                        return;
                    }

                    aws_base64_encode(&toEncode, &output);
                }
            }
        }

    } // namespace Crt
} // namespace Aws
