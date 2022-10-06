#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/crt/Types.h>

struct aws_endpoints_rule_engine;
struct aws_endpoints_request_context;
struct aws_endpoints_resolved_endpoint;

namespace Aws
{
    namespace Crt
    {
        namespace Endpoints
        {
            class AWS_CRT_CPP_API RequestContext final
            {
              public:
                RequestContext(Allocator *allocator = ApiAllocator()) noexcept;
                ~RequestContext();

                /* TODO: move/copy semantics */
                RequestContext(const RequestContext &) = delete;
                RequestContext &operator=(const RequestContext &) = delete;
                RequestContext(RequestContext &&) = delete;
                RequestContext &operator=(RequestContext &&) = delete;

                /**
                 * @return true if the instance is in a valid state, false otherwise.
                 */
                operator bool() const noexcept { return m_requestContext != nullptr; }

                int AddString(const ByteCursor &name, const ByteCursor &value);

                int AddBoolean(const ByteCursor &name, bool value);

                /// @private
                aws_endpoints_request_context *GetNativeHandle() const noexcept { return m_requestContext; }

              private:
                Allocator *m_allocator;
                aws_endpoints_request_context *m_requestContext;
            };

            class AWS_CRT_CPP_API ResolutionOutcome final
            {
              public:
                ~ResolutionOutcome();

                /* TODO: move/copy semantics */
                ResolutionOutcome(const ResolutionOutcome &) = delete;
                ResolutionOutcome &operator=(const ResolutionOutcome &) = delete;
                ResolutionOutcome(ResolutionOutcome &&toMove) noexcept;
                ResolutionOutcome &operator=(ResolutionOutcome &&) = delete;

                bool IsEndpoint() const noexcept;
                bool IsError() const noexcept;

                Optional<ByteCursor> getUrl() const noexcept;
                Optional<ByteCursor> getProperties() const noexcept;
                Optional<UnorderedMap<StringView, Vector<StringView>>> getHeaders() const noexcept;

                Optional<ByteCursor> getError() const noexcept;

                /**
                 * @return true if the instance is in a valid state, false otherwise.
                 */
                operator bool() const noexcept { return m_resolvedEndpoint != nullptr; }

                /// @private For use by rule engine.
                ResolutionOutcome(aws_endpoints_resolved_endpoint *impl, Allocator *allocator);

              private:
                Allocator *m_allocator;
                aws_endpoints_resolved_endpoint *m_resolvedEndpoint;
            };

            /**
             * Contains endpoints rule engine.
             */
            class AWS_CRT_CPP_API RuleEngine final
            {
              public:
                RuleEngine(const ByteCursor &rulesetCursor, Allocator *allocator = ApiAllocator()) noexcept;
                ~RuleEngine();

                /* TODO: move/copy semantics */
                RuleEngine(const RuleEngine &) = delete;
                RuleEngine &operator=(const RuleEngine &) = delete;
                RuleEngine(RuleEngine &&) = delete;
                RuleEngine &operator=(RuleEngine &&) = delete;

                /**
                 * @return true if the instance is in a valid state, false otherwise.
                 */
                operator bool() const noexcept { return m_ruleEngine != nullptr; }

                Optional<ResolutionOutcome> resolve(const RequestContext &context);

              private:
                Allocator *m_allocator;
                aws_endpoints_rule_engine *m_ruleEngine;
            };
        } // namespace Endpoints

    } // namespace Crt

} // namespace Aws
