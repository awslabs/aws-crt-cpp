/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#include <aws/crt/endpoints/BddEngine.h>
#include <aws/sdkutils/endpoints_bdd_engine.h>
#include <aws/sdkutils/partitions.h>

namespace Aws
{
    namespace Crt
    {
        namespace Endpoints
        {
            static void s_initEngine(
                Allocator *allocator,
                aws_endpoints_bdd_engine **engine,
                const ByteCursor &bytecodeCursor,
                const ByteCursor &partitionsCursor)
            {
                auto partitions = aws_partitions_config_new_from_string(allocator, partitionsCursor);
                if (partitions != nullptr)
                {
                    *engine = aws_endpoints_bdd_engine_new_from_bytecode(allocator, bytecodeCursor, partitions);
                    aws_partitions_config_release(partitions);
                }
            }

            BddEngine::BddEngine(
                Allocator *allocator,
                const char *bytecodePath,
                const ByteCursor &partitionsCursor) noexcept
                : m_bytecodeBuf(ByteBufInit(allocator, 0)), m_engine(nullptr)
            {
                if (ByteBufInitFromFile(m_bytecodeBuf, allocator, bytecodePath))
                {
                    ByteCursor cursor = ByteCursorFromByteBuf(m_bytecodeBuf);
                    s_initEngine(allocator, &m_engine, cursor, partitionsCursor);
                }
            }

            BddEngine::BddEngine(
                Allocator *allocator,
                const ByteBuf &bytecodeBuffer,
                const ByteCursor &partitionsCursor) noexcept
                : m_bytecodeBuf(ByteBufNewCopy(allocator, bytecodeBuffer.buffer, bytecodeBuffer.len)),
                  m_engine(nullptr)
            {
                ByteCursor cursor = ByteCursorFromByteBuf(m_bytecodeBuf);
                s_initEngine(allocator, &m_engine, cursor, partitionsCursor);
            }

            BddEngine::BddEngine(
                Allocator *allocator,
                const ByteCursor &bytecodeCursor,
                const ByteCursor &partitionsCursor) noexcept
                : m_bytecodeBuf(ByteBufInit(allocator, 0)), m_engine(nullptr)
            {
                s_initEngine(allocator, &m_engine, bytecodeCursor, partitionsCursor);
            }

            BddEngine::~BddEngine()
            {
                m_engine = aws_endpoints_bdd_engine_release(m_engine);
                ByteBufDelete(m_bytecodeBuf);
            }

            Optional<ResolutionOutcome> BddEngine::Resolve(const RequestContext &context) const
            {
                aws_endpoints_resolved_endpoint *resolved = nullptr;
                if (aws_endpoints_bdd_engine_resolve(m_engine, context.GetNativeHandle(), &resolved))
                {
                    return Optional<ResolutionOutcome>();
                }
                return Optional<ResolutionOutcome>(ResolutionOutcome(resolved));
            }
        } // namespace Endpoints
    } // namespace Crt
} // namespace Aws
