/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/common/string.h>
#include <aws/crt/endpoints/RuleEngine.h>
#include <aws/sdkutils/endpoints_rule_engine.h>

namespace Aws
{
    namespace Crt
    {
        namespace Endpoints
        {
            RequestContext::RequestContext(Allocator *allocator) noexcept : m_allocator(allocator)
            {
                m_requestContext = aws_endpoints_request_context_new(allocator);
            }

            RequestContext::~RequestContext()
            {
                m_requestContext = aws_endpoints_request_context_release(m_requestContext);
            }

            int RequestContext::AddString(const ByteCursor &name, const ByteCursor &value)
            {
                return aws_endpoints_request_context_add_string(m_allocator, m_requestContext, name, value);
            }

            int RequestContext::AddBoolean(const ByteCursor &name, bool value)
            {
                return aws_endpoints_request_context_add_boolean(m_allocator, m_requestContext, name, value);
            }

            ResolutionOutcome::ResolutionOutcome(aws_endpoints_resolved_endpoint *impl, Allocator *allocator)
                : m_resolvedEndpoint(impl), m_allocator(allocator)
            {
            }

            ResolutionOutcome::~ResolutionOutcome()
            {
                aws_endpoints_resolved_endpoint_release(m_resolvedEndpoint);
            }

            bool ResolutionOutcome::IsEndpoint() const noexcept
            {
                return AWS_ENDPOINTS_RESOLVED_ENDPOINT == aws_endpoints_resolved_endpoint_get_type(m_resolvedEndpoint);
            }

            bool ResolutionOutcome::IsError() const noexcept
            {
                return AWS_ENDPOINTS_RESOLVED_ERROR == aws_endpoints_resolved_endpoint_get_type(m_resolvedEndpoint);
            }

            Optional<ByteCursor> ResolutionOutcome::getUrl() const noexcept
            {
                ByteCursor url;
                if (aws_endpoints_resolved_endpoint_get_url(m_resolvedEndpoint, &url))
                {
                    return Optional<ByteCursor>();
                }

                return Optional<ByteCursor>(url);
            }

            inline StringView CrtStringToStringView(const aws_string *s)
            {
                ByteCursor key = aws_byte_cursor_from_string(s);
                return ByteCursorToStringView(key);
            }

            Optional<UnorderedMap<StringView, Vector<StringView>>> ResolutionOutcome::getHeaders() const noexcept
            {
                const aws_hash_table *resolved_headers = nullptr;

                if (aws_endpoints_resolved_endpoint_get_headers(m_resolvedEndpoint, &resolved_headers))
                {
                    return Optional<UnorderedMap<StringView, Vector<StringView>>>();
                }

                UnorderedMap<StringView, Vector<StringView>> headers;
                for (struct aws_hash_iter iter = aws_hash_iter_begin(resolved_headers); !aws_hash_iter_done(&iter);
                     aws_hash_iter_next(&iter))
                {
                    ByteCursor key = aws_byte_cursor_from_string((const aws_string *)iter.element.key);
                    const aws_array_list *array = (const aws_array_list *)iter.element.value;
                    headers.insert(
                        {ByteCursorToStringView(key),
                         ArrayListToVector<aws_string *, StringView>(array, CrtStringToStringView)});
                }

                return Optional<UnorderedMap<StringView, Vector<StringView>>>(headers);
            }

            Optional<ByteCursor> ResolutionOutcome::getProperties() const noexcept
            {
                ByteCursor properties;
                if (aws_endpoints_resolved_endpoint_get_properties(m_resolvedEndpoint, &properties))
                {
                    return Optional<ByteCursor>();
                }

                return Optional<ByteCursor>(properties);
            }

            Optional<ByteCursor> ResolutionOutcome::getError() const noexcept
            {
                ByteCursor error;
                if (aws_endpoints_resolved_endpoint_get_error(m_resolvedEndpoint, &error))
                {
                    return Optional<ByteCursor>();
                }

                return Optional<ByteCursor>(error);
            }

            RuleEngine::RuleEngine(const ByteCursor &rulesetCursor, Allocator *allocator) noexcept
                : m_allocator(allocator), m_ruleEngine(nullptr)
            {
                auto ruleset = aws_endpoints_ruleset_new_from_string(allocator, rulesetCursor);
                if (ruleset)
                {
                    m_ruleEngine = aws_endpoints_rule_engine_new(allocator, ruleset);
                }
            };

            RuleEngine::~RuleEngine()
            {
                m_ruleEngine = aws_endpoints_rule_engine_release(m_ruleEngine);
            }
        } // namespace Endpoints
    }     // namespace Crt
} // namespace Aws
