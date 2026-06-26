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
                 * Construct from a file path to compiled bytecode.
                 * The engine reads and owns the bytecode for its lifetime.
                 */
                BddEngine(Allocator *allocator, const char *bytecodePath, const ByteCursor &partitionsCursor) noexcept;

                /**
                 * Construct from a bytecode buffer.
                 * The engine copies the buffer and owns it for its lifetime.
                 */
                BddEngine(
                    Allocator *allocator,
                    const ByteBuf &bytecodeBuffer,
                    const ByteCursor &partitionsCursor) noexcept;

                /**
                 * Construct from a bytecode cursor.
                 * The caller is responsible for keeping the underlying buffer alive
                 * for the lifetime of this engine.
                 */
                BddEngine(
                    Allocator *allocator,
                    const ByteCursor &bytecodeCursor,
                    const ByteCursor &partitionsCursor) noexcept;

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
                ByteBuf m_bytecodeBuf;
                aws_endpoints_bdd_engine *m_engine;
            };
        } // namespace Endpoints
    } // namespace Crt
} // namespace Aws
