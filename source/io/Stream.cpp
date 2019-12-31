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

#include <aws/crt/StlAllocator.h>
#include <aws/crt/io/Stream.h>

#include <aws/io/stream.h>

namespace Aws
{
    namespace Crt
    {
        namespace Io
        {
            InputStream::~InputStream()
            {
                if (m_underlying_stream)
                {
                    aws_input_stream_destroy(m_underlying_stream);
                    aws_mem_release(m_allocator, m_underlying_stream);
                    m_underlying_stream = nullptr;
                }
            }

            bool InputStream::IsGood() const noexcept { return m_good; }

            int InputStream::s_Seek(aws_input_stream *stream, aws_off_t offset, enum aws_stream_seek_basis basis)
            {
                auto impl = static_cast<InputStream *>(stream->impl);

                if (impl->Seek(offset, static_cast<SeekBasis>(basis)))
                {
                    return AWS_OP_SUCCESS;
                }

                impl->m_good = false;
                return AWS_OP_ERR;
            }

            int InputStream::s_Read(aws_input_stream *stream, aws_byte_buf *dest)
            {
                auto impl = static_cast<InputStream *>(stream->impl);

                if (impl->Read(*dest))
                {
                    return AWS_OP_SUCCESS;
                }

                impl->m_good = false;
                return AWS_OP_ERR;
            }

            int InputStream::s_GetStatus(aws_input_stream *stream, aws_stream_status *status)
            {
                auto impl = static_cast<InputStream *>(stream->impl);

                *status = impl->GetStatus();
                return AWS_OP_SUCCESS;
            }

            int InputStream::s_GetLength(struct aws_input_stream *stream, int64_t *out_length)
            {
                auto impl = static_cast<InputStream *>(stream->impl);

                int64_t length = impl->GetLength();

                if (length != -1)
                {
                    *out_length = length;
                    return AWS_OP_SUCCESS;
                }

                impl->m_good = false;
                return AWS_OP_ERR;
            }

            void InputStream::s_Destroy(struct aws_input_stream *stream)
            {
                // DO NOTHING, let the C++ destructor handle it.
            }

            aws_input_stream_vtable InputStream::s_vtable = {
                InputStream::s_Seek,
                InputStream::s_Read,
                InputStream::s_GetStatus,
                InputStream::s_GetLength,
                InputStream::s_Destroy,
            };

            InputStream::InputStream(Aws::Crt::Allocator *allocator)
            {
                m_allocator = allocator;
                m_underlying_stream =
                    static_cast<aws_input_stream *>(aws_mem_calloc(m_allocator, 1, sizeof(aws_input_stream)));

                if (!m_underlying_stream)
                {
                    m_good = false;
                    return;
                }

                m_good = true;
                m_underlying_stream->impl = this;
                m_underlying_stream->allocator = m_allocator;
                m_underlying_stream->vtable = &s_vtable;
            }

            StdIOStreamInputStream::StdIOStreamInputStream(
                std::shared_ptr<Aws::Crt::Io::IStream> stream,
                Aws::Crt::Allocator *allocator) noexcept
                : InputStream(allocator), m_stream(std::move(stream))
            {
            }

            bool StdIOStreamInputStream::Read(ByteBuf &buffer) noexcept
            {
                m_stream->read(reinterpret_cast<char *>(buffer.buffer + buffer.len), buffer.capacity - buffer.len);
                buffer.len += static_cast<size_t>(m_stream->gcount());

                return true;
            }

            StreamStatus StdIOStreamInputStream::GetStatus() const noexcept
            {
                StreamStatus status;
                status.is_end_of_stream = m_stream->eof();
                status.is_valid = m_stream->good();

                return status;
            }

            int64_t StdIOStreamInputStream::GetLength() const noexcept
            {
                auto currentPosition = m_stream->tellg();

                m_stream->seekg(0, std::ios_base::end);
                int64_t retVal = -1;

                if (m_stream->good())
                {
                    retVal = static_cast<int64_t>(m_stream->tellg());
                }

                m_stream->seekg(currentPosition);

                return retVal;
            }

            bool StdIOStreamInputStream::Seek(OffsetType offsetType, SeekBasis seekBasis) noexcept
            {
                // very important, otherwise the stream can't be reused after reading the entire stream the first time.
                m_stream->clear();

                auto seekDir = std::ios_base::beg;
                switch (seekBasis)
                {
                    case SeekBasis::Begin:
                        seekDir = std::ios_base::beg;
                        break;
                    case SeekBasis::End:
                        seekDir = std::ios_base::end;
                        break;
                    default:
                        return false;
                }

                m_stream->seekg(Aws::Crt::Io::IStream::off_type(offsetType), seekDir);

                return true;
            }
        } // namespace Io
    }     // namespace Crt
} // namespace Aws
