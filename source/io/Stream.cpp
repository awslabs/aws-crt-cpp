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

#include <aws/crt/io/Stream.h>

#include <aws/io/stream.h>

static std::ios_base::seekdir s_stream_seek_basis_to_seekdir(enum aws_stream_seek_basis basis)
{
    switch (basis)
    {
        case AWS_SSB_BEGIN:
            return std::ios_base::beg;

        case AWS_SSB_END:
            return std::ios_base::end;
    }

    return std::ios_base::beg;
}

struct aws_input_stream_cpp_impl
{
    std::shared_ptr<Aws::Crt::Io::IStream> stream;
};

static int s_aws_input_stream_cpp_seek(
    struct aws_input_stream *stream,
    aws_off_t offset,
    enum aws_stream_seek_basis basis)
{
    aws_input_stream_cpp_impl *impl = static_cast<aws_input_stream_cpp_impl *>(stream->impl);
    impl->stream->seekg(Aws::Crt::Io::IStream::off_type(offset), s_stream_seek_basis_to_seekdir(basis));

    return AWS_OP_SUCCESS;
}

static int s_aws_input_stream_cpp_read(struct aws_input_stream *stream, struct aws_byte_buf *dest)
{
    aws_input_stream_cpp_impl *impl = static_cast<aws_input_stream_cpp_impl *>(stream->impl);

    impl->stream->read(reinterpret_cast<char *>(dest->buffer + dest->len), dest->capacity - dest->len);
    dest->len += static_cast<size_t>(impl->stream->gcount());

    return AWS_OP_SUCCESS;
}

static int s_aws_input_stream_cpp_get_status(struct aws_input_stream *stream, struct aws_stream_status *status)
{
    aws_input_stream_cpp_impl *impl = static_cast<aws_input_stream_cpp_impl *>(stream->impl);

    status->is_end_of_stream = impl->stream->eof();
    status->is_valid = impl->stream->good();

    return AWS_OP_SUCCESS;
}

static int s_aws_input_stream_cpp_get_length(struct aws_input_stream *stream, int64_t *length)
{
    aws_input_stream_cpp_impl *impl = static_cast<aws_input_stream_cpp_impl *>(stream->impl);

    auto currentPosition = impl->stream->tellg();

    impl->stream->seekg(0, std::ios_base::end);

    if (impl->stream->good())
    {
        *length = static_cast<int64_t>(impl->stream->tellg());
    }
    else
    {
        *length = 0;
    }

    impl->stream->seekg(currentPosition);

    return AWS_OP_SUCCESS;
}

static void s_aws_input_stream_cpp_destroy(struct aws_input_stream *stream)
{
    aws_input_stream_cpp_impl *impl = static_cast<aws_input_stream_cpp_impl *>(stream->impl);
    impl->stream = nullptr;
    aws_mem_release(stream->allocator, stream);
}

static struct aws_input_stream_vtable s_aws_input_stream_cpp_vtable = {s_aws_input_stream_cpp_seek,
                                                                       s_aws_input_stream_cpp_read,
                                                                       s_aws_input_stream_cpp_get_status,
                                                                       s_aws_input_stream_cpp_get_length,
                                                                       s_aws_input_stream_cpp_destroy};

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            struct aws_input_stream *AwsInputStreamNewCpp(
                const std::shared_ptr<Aws::Crt::Io::IStream> &stream,
                Aws::Crt::Allocator *allocator) noexcept
            {
                struct aws_input_stream *input_stream = NULL;
                struct aws_input_stream_cpp_impl *impl = NULL;

                aws_mem_acquire_many(
                    allocator,
                    2,
                    &input_stream,
                    sizeof(struct aws_input_stream),
                    &impl,
                    sizeof(struct aws_input_stream_cpp_impl));

                if (!input_stream)
                {
                    return NULL;
                }

                AWS_ZERO_STRUCT(*input_stream);
                AWS_ZERO_STRUCT(*impl);

                input_stream->allocator = allocator;
                input_stream->vtable = &s_aws_input_stream_cpp_vtable;
                input_stream->impl = impl;

                impl->stream = stream;

                return input_stream;
            }
        } // namespace Io
    }     // namespace Crt
} // namespace Aws
