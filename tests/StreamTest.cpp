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

#include <aws/crt/io/Stream.h>

#include <aws/common/byte_buf.h>
#include <aws/io/stream.h>

#include <aws/testing/aws_test_harness.h>

#include <sstream>

static int s_StreamTestCreateDestroyWrapperFirst(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        auto string_stream = std::make_shared<std::stringstream>("SomethingInteresting");
        auto stream = std::static_pointer_cast<Aws::Crt::Io::IStream>(string_stream);

        aws_input_stream *wrapped_stream = Aws::Crt::Io::AwsInputStreamNewCpp(stream);

        ASSERT_TRUE(wrapped_stream != nullptr);

        aws_input_stream_destroy(wrapped_stream);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(StreamTestCreateDestroyWrapperFirst, s_StreamTestCreateDestroyWrapperFirst)

static int s_StreamTestCreateDestroyWrapperLast(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        aws_input_stream *wrapped_stream = nullptr;

        {
            auto string_stream = std::make_shared<std::stringstream>("SomethingInteresting");
            auto stream = std::static_pointer_cast<Aws::Crt::Io::IStream>(string_stream);

            wrapped_stream = Aws::Crt::Io::AwsInputStreamNewCpp(stream);

            ASSERT_TRUE(wrapped_stream != nullptr);
        }

        aws_input_stream_destroy(wrapped_stream);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(StreamTestCreateDestroyWrapperLast, s_StreamTestCreateDestroyWrapperLast)

static const char *STREAM_CONTENTS = "SomeContents";

static int s_StreamTestLength(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        auto string_stream = std::make_shared<std::stringstream>(STREAM_CONTENTS);
        auto stream = std::static_pointer_cast<Aws::Crt::Io::IStream>(string_stream);

        aws_input_stream *wrapped_stream = Aws::Crt::Io::AwsInputStreamNewCpp(stream);

        int64_t length = 0;
        ASSERT_SUCCESS(aws_input_stream_get_length(wrapped_stream, &length));
        ASSERT_TRUE(length == strlen(STREAM_CONTENTS));

        aws_input_stream_destroy(wrapped_stream);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(StreamTestLength, s_StreamTestLength)

static int s_StreamTestRead(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        auto string_stream = std::make_shared<Aws::Crt::StringStream>(STREAM_CONTENTS);

        aws_input_stream *wrapped_stream = Aws::Crt::Io::AwsInputStreamNewCpp(string_stream);

        aws_byte_buf buffer;
        AWS_ZERO_STRUCT(buffer);
        aws_byte_buf_init(&buffer, allocator, 256);

        aws_input_stream_read(wrapped_stream, &buffer);

        ASSERT_TRUE(buffer.len == strlen(STREAM_CONTENTS));
        ASSERT_BIN_ARRAYS_EQUALS(STREAM_CONTENTS, buffer.len, buffer.buffer, buffer.len);

        aws_byte_buf_clean_up(&buffer);
        aws_input_stream_destroy(wrapped_stream);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(StreamTestRead, s_StreamTestRead)

static const aws_off_t BEGIN_SEEK_OFFSET = 4;

static int s_StreamTestSeekBegin(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        auto string_stream = std::make_shared<std::stringstream>(STREAM_CONTENTS);
        auto stream = std::static_pointer_cast<Aws::Crt::Io::IStream>(string_stream);

        aws_input_stream *wrapped_stream = Aws::Crt::Io::AwsInputStreamNewCpp(stream);

        ASSERT_SUCCESS(aws_input_stream_seek(wrapped_stream, BEGIN_SEEK_OFFSET, AWS_SSB_BEGIN));

        aws_byte_buf buffer;
        AWS_ZERO_STRUCT(buffer);
        aws_byte_buf_init(&buffer, allocator, 256);

        aws_input_stream_read(wrapped_stream, &buffer);

        ASSERT_TRUE(buffer.len == strlen(STREAM_CONTENTS) - BEGIN_SEEK_OFFSET);
        ASSERT_BIN_ARRAYS_EQUALS(STREAM_CONTENTS + BEGIN_SEEK_OFFSET, buffer.len, buffer.buffer, buffer.len);

        aws_byte_buf_clean_up(&buffer);
        aws_input_stream_destroy(wrapped_stream);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(StreamTestSeekBegin, s_StreamTestSeekBegin)

static const aws_off_t END_SEEK_OFFSET = -4;

static int s_StreamTestSeekEnd(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        auto string_stream = std::make_shared<std::stringstream>(STREAM_CONTENTS);
        auto stream = std::static_pointer_cast<Aws::Crt::Io::IStream>(string_stream);

        aws_input_stream *wrapped_stream = Aws::Crt::Io::AwsInputStreamNewCpp(stream);

        ASSERT_SUCCESS(aws_input_stream_seek(wrapped_stream, END_SEEK_OFFSET, AWS_SSB_END));

        aws_byte_buf buffer;
        AWS_ZERO_STRUCT(buffer);
        aws_byte_buf_init(&buffer, allocator, 256);

        aws_input_stream_read(wrapped_stream, &buffer);

        ASSERT_TRUE(buffer.len == -END_SEEK_OFFSET);
        ASSERT_BIN_ARRAYS_EQUALS(
            STREAM_CONTENTS + strlen(STREAM_CONTENTS) + END_SEEK_OFFSET, buffer.len, buffer.buffer, buffer.len);

        aws_byte_buf_clean_up(&buffer);
        aws_input_stream_destroy(wrapped_stream);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(StreamTestSeekEnd, s_StreamTestSeekEnd)
