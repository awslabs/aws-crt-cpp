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
            BddEngine::BddEngine(
                const ByteCursor &bytecodeBuffer,
                const ByteCursor &partitionsCursor,
                Allocator *allocator) noexcept
                : m_engine(nullptr)
            {
                auto partitions = aws_partitions_config_new_from_string(allocator, partitionsCursor);
                if (partitions != nullptr)
                {
                    m_engine = aws_endpoints_bdd_engine_new_from_bytecode(allocator, bytecodeBuffer, partitions);
                    aws_partitions_config_release(partitions);
                }
            }

            BddEngine::~BddEngine()
            {
                m_engine = aws_endpoints_bdd_engine_release(m_engine);
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
