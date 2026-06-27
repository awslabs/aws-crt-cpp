#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Types.h>
#include <aws/crt/endpoints/RuleEngine.h>

struct aws_endpoints_bdd_engine;

namespace Aws
{
    namespace Crt
    {
        namespace Endpoints
        {
            /**
             * Endpoints BDD Engine.
             *
             * Resolves endpoints from compiled bytecode rather than a JSON ruleset.
             * The caller is responsible for keeping the bytecode buffer alive for
             * the lifetime of this engine.
             *
             * RequestContext and ResolutionOutcome are shared with RuleEngine and
             * are used identically.
             */
            class AWS_CRT_CPP_API BddEngine final
            {
              public:
                /**
                 * Construct from a bytecode cursor and partitions JSON.
                 * The caller is responsible for keeping the bytecode buffer alive
                 * for the lifetime of this engine.
                 */
                BddEngine(
                    Allocator *allocator,
                    const ByteCursor &bytecodeCursor,
                    const ByteCursor &partitionsCursor) noexcept;

                ~BddEngine() = default;

                BddEngine(const BddEngine &) = delete;
                BddEngine &operator=(const BddEngine &) = delete;
                BddEngine(BddEngine &&) = delete;
                BddEngine &operator=(BddEngine &&) = delete;

                /**
                 * @return true if the engine was constructed successfully.
                 */
                operator bool() const noexcept { return m_engine != nullptr; }

                /**
                 * Resolve an endpoint from the provided request context.
                 * Returns None on failure; use Aws::Crt::LastError() to retrieve the error code.
                 */
                Optional<ResolutionOutcome> Resolve(const RequestContext &context) const;

              private:
                ScopedResource<struct aws_endpoints_bdd_engine> m_engine;
            };
        } // namespace Endpoints
    } // namespace Crt
} // namespace Aws
