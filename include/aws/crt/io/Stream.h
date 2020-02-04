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
#include <aws/io/stream.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            using StreamStatus = aws_stream_status;
            using OffsetType = aws_off_t;

            enum class StreamSeekBasis
            {
                Begin = AWS_SSB_BEGIN,
                End = AWS_SSB_END,
            };

            /***
             * Interface for building an Object oriented stream that will be honored by the CRT's low-level
             * aws_input_stream interface. To use, create a subclass of InputStream and define the abstract
             * functions.
             */
            class AWS_CRT_CPP_API InputStream
            {
              public:
                virtual ~InputStream();

                InputStream(const InputStream &) = delete;
                InputStream &operator=(const InputStream &) = delete;
                InputStream(InputStream &&) = delete;
                InputStream &operator=(InputStream &&) = delete;

                explicit operator bool() const noexcept { return IsValid(); }
                virtual bool IsValid() const noexcept = 0;
                
		int64_t GetLength() const noexcept;

                aws_input_stream *GetUnderlyingStream() noexcept { return &m_underlying_stream; }

              protected:
                Allocator *m_allocator;
                aws_input_stream m_underlying_stream;

                InputStream(Aws::Crt::Allocator *allocator = DefaultAllocator());

                /***
                 * Read up-to buffer::capacity - buffer::len into buffer::buffer
                 * Increment buffer::len by the amount you read in.
                 *
                 * @return true on success, false otherwise. Return false, when there is nothing left to read.
                 * You SHOULD raise an error via aws_raise_error()
                 * if an actual failure condition occurs.
                 */
                virtual bool ReadImpl(ByteBuf &buffer) noexcept = 0;

                /**
                 * Returns the current status of the stream.
                 */
                virtual StreamStatus GetStatusImpl() const noexcept = 0;

                /**
                 * Returns the total length of the available data for the stream.
                 * Returns -1 if not available.
                 */
                virtual int64_t GetLengthImpl() const noexcept = 0;

                /**
                 * Seek's the stream to seekBasis based offset bytes.
                 *
                 * It is expected, that if seeking to the beginning of a stream,
                 * all error's are cleared if possible.
                 *
                 * @return true on success, false otherwise. You SHOULD raise an error via aws_raise_error()
                 * if a failure occurs.
                 */
                virtual bool SeekImpl(OffsetType offset, StreamSeekBasis seekBasis) noexcept = 0;

              private:
                static int s_Seek(aws_input_stream *stream, aws_off_t offset, enum aws_stream_seek_basis basis);
                static int s_Read(aws_input_stream *stream, aws_byte_buf *dest);
                static int s_GetStatus(aws_input_stream *stream, aws_stream_status *status);
                static int s_GetLength(struct aws_input_stream *stream, int64_t *out_length);
                static void s_Destroy(struct aws_input_stream *stream);

                static aws_input_stream_vtable s_vtable;
            };

            /***
             * Implementation of Aws::Crt::Io::InputStream that wraps a std::input_stream.
             */
            class AWS_CRT_CPP_API StdIOStreamInputStream : public InputStream
            {
              public:
                StdIOStreamInputStream(
                    std::shared_ptr<Aws::Crt::Io::IStream> stream,
                    Aws::Crt::Allocator *allocator = DefaultAllocator()) noexcept;

                bool IsValid() const noexcept override;

              protected:
                bool ReadImpl(ByteBuf &buffer) noexcept override;
                StreamStatus GetStatusImpl() const noexcept override;
                int64_t GetLengthImpl() const noexcept override;
                bool SeekImpl(OffsetType offsetType, StreamSeekBasis seekBasis) noexcept override;

              private:
                std::shared_ptr<Aws::Crt::Io::IStream> m_stream;
            };
        } // namespace Io
    }     // namespace Crt
} // namespace Aws
