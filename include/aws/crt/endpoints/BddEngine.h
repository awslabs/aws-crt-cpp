#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

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
             * The bytecode buffer passed to the constructor must remain valid for
             * the lifetime of this engine.
             *
             * RequestContext and ResolutionOutcome are shared with RuleEngine and
             * are used identically.
             */
            class AWS_CRT_CPP_API BddEngine final
            {
              public:
                /**
                 * Construct from compiled endpoint ruleset bytecode and partitions JSON.
                 *
                 * @param bytecodeBuffer Cursor into compiled bytecode. Caller owns the
                 *        buffer and must keep it valid for the lifetime of this engine.
                 * @param partitionsCursor Partitions JSON string.
                 * @param allocator Memory allocator.
                 */
                BddEngine(
                    const ByteCursor &bytecodeBuffer,
                    const ByteCursor &partitionsCursor,
                    Allocator *allocator = ApiAllocator()) noexcept;
                ~BddEngine();

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
                aws_endpoints_bdd_engine *m_engine;
            };
        } // namespace Endpoints
    } // namespace Crt
} // namespace Aws
