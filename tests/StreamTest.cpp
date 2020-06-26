/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/Api.h>

#include <aws/crt/io/Stream.h>

#include <aws/common/byte_buf.h>
#include <aws/io/stream.h>

#include <aws/testing/aws_test_harness.h>

#include <sstream>

static int s_StreamTestCreateDestroyWrapper(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        auto stringStream = Aws::Crt::MakeShared<std::stringstream>(allocator, "SomethingInteresting");
        Aws::Crt::Io::StdIOStreamInputStream inputStream(stringStream, allocator);

        ASSERT_TRUE(static_cast<bool>(inputStream));
        ASSERT_NOT_NULL(inputStream.GetUnderlyingStream());
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(StreamTestCreateDestroyWrapper, s_StreamTestCreateDestroyWrapper)

static const char *STREAM_CONTENTS = "SomeContents";

static int s_StreamTestLength(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        auto stringStream = Aws::Crt::MakeShared<std::stringstream>(allocator, STREAM_CONTENTS);

        Aws::Crt::Io::StdIOStreamInputStream wrappedStream(stringStream, allocator);

        int64_t length = 0;
        ASSERT_SUCCESS(aws_input_stream_get_length(wrappedStream.GetUnderlyingStream(), &length));
        ASSERT_TRUE(length == strlen(STREAM_CONTENTS));
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(StreamTestLength, s_StreamTestLength)

static int s_StreamTestRead(struct aws_allocator *allocator, void *ctx)
{
    (void)ctx;
    Aws::Crt::ApiHandle apiHandle(allocator);

    {
        auto stringStream = Aws::Crt::MakeShared<Aws::Crt::StringStream>(allocator, STREAM_CONTENTS);

        Aws::Crt::Io::StdIOStreamInputStream wrappedStream(stringStream, allocator);

        aws_byte_buf buffer;
        AWS_ZERO_STRUCT(buffer);
        aws_byte_buf_init(&buffer, allocator, 256);

        aws_input_stream_read(wrappedStream.GetUnderlyingStream(), &buffer);

        ASSERT_TRUE(buffer.len == strlen(STREAM_CONTENTS));
        ASSERT_BIN_ARRAYS_EQUALS(STREAM_CONTENTS, buffer.len, buffer.buffer, buffer.len);

        aws_byte_buf_clean_up(&buffer);
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
        auto stringStream = Aws::Crt::MakeShared<Aws::Crt::StringStream>(allocator, STREAM_CONTENTS);

        Aws::Crt::Io::StdIOStreamInputStream wrappedStream(stringStream, allocator);

        ASSERT_SUCCESS(aws_input_stream_seek(wrappedStream.GetUnderlyingStream(), BEGIN_SEEK_OFFSET, AWS_SSB_BEGIN));

        aws_byte_buf buffer;
        AWS_ZERO_STRUCT(buffer);
        aws_byte_buf_init(&buffer, allocator, 256);

        aws_input_stream_read(wrappedStream.GetUnderlyingStream(), &buffer);

        ASSERT_TRUE(buffer.len == strlen(STREAM_CONTENTS) - BEGIN_SEEK_OFFSET);
        ASSERT_BIN_ARRAYS_EQUALS(STREAM_CONTENTS + BEGIN_SEEK_OFFSET, buffer.len, buffer.buffer, buffer.len);

        aws_byte_buf_clean_up(&buffer);
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
        auto stringStream = Aws::Crt::MakeShared<Aws::Crt::StringStream>(allocator, STREAM_CONTENTS);

        Aws::Crt::Io::StdIOStreamInputStream wrappedStream(stringStream, allocator);

        ASSERT_SUCCESS(aws_input_stream_seek(wrappedStream.GetUnderlyingStream(), END_SEEK_OFFSET, AWS_SSB_END));

        aws_byte_buf buffer;
        AWS_ZERO_STRUCT(buffer);
        aws_byte_buf_init(&buffer, allocator, 256);

        aws_input_stream_read(wrappedStream.GetUnderlyingStream(), &buffer);

        ASSERT_TRUE(buffer.len == -END_SEEK_OFFSET);
        ASSERT_BIN_ARRAYS_EQUALS(
            STREAM_CONTENTS + strlen(STREAM_CONTENTS) + END_SEEK_OFFSET, buffer.len, buffer.buffer, buffer.len);

        aws_byte_buf_clean_up(&buffer);
    }

    return AWS_OP_SUCCESS;
}

AWS_TEST_CASE(StreamTestSeekEnd, s_StreamTestSeekEnd)
