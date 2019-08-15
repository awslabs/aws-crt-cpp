#pragma once
/*
 * Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
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

#include <aws/common/error.h>

#include <utility>

namespace Aws
{
    namespace Crt
    {
        template <typename R, typename E> // Result, Error
        class Outcome
        {
          public:
            Outcome() : m_success(false) {}

            Outcome(const R &r) : m_result(r), m_success(true) {}

            Outcome(const E &e) : m_error(e), m_success(false) {}

            Outcome(R &&r) : m_result(std::move(r)), m_success(true) {}

            Outcome(E &&e) : m_error(std::move(e)), m_success(false) {}

            Outcome(const Outcome &o) : m_result(o.m_result), m_error(o.m_error), m_success(o.m_success) {}

            Outcome &operator=(const Outcome &o)
            {
                if (this != &o)
                {
                    m_result = o.m_result;
                    m_error = o.m_error;
                    m_success = o.m_success;
                }

                return *this;
            }

            Outcome(Outcome &&o)
                : // Required to force Move Constructor
                  m_result(std::move(o.m_result)), m_error(std::move(o.m_error)), m_success(o.m_success)
            {
            }

            Outcome &operator=(Outcome &&o)
            {
                if (this != &o)
                {
                    m_result = std::move(o.m_result);
                    m_error = std::move(o.m_error);
                    m_success = o.m_success;
                }

                return *this;
            }

            const R &GetResult() const { return m_result; }

            R &GetResult() { return m_result; }

            const E &GetError() const { return m_error; }

            operator bool() const { return m_success; }

          private:
            R m_result;
            E m_error;
            bool m_success;
        };

        /*
         * Specialization for Error-only (void result)
         */
        template <typename E> // Error
        class Outcome<void, E>
        {
          public:
            Outcome() : m_success(true) {}

            Outcome(const E &e) : m_error(e), m_success(false) {}

            Outcome(E &&e) : m_error(std::move(e)), m_success(false) {}

            Outcome(const Outcome &o) : m_error(o.m_error), m_success(o.m_success) {}

            Outcome &operator=(const Outcome &o)
            {
                if (this != &o)
                {
                    m_error = o.m_error;
                    m_success = o.m_success;
                }

                return *this;
            }

            Outcome(Outcome &&o)
                : // Required to force Move Constructor
                  m_error(std::move(o.m_error)), m_success(o.m_success)
            {
            }

            Outcome &operator=(Outcome &&o)
            {
                if (this != &o)
                {
                    m_error = std::move(o.m_error);
                    m_success = o.m_success;
                }

                return *this;
            }

            const E &GetError() const { return m_error; }

            operator bool() const { return m_success; }

          private:
            E m_error;
            bool m_success;
        };

        template <typename R> using AwsCrtResult = Outcome<R, int>;
        using AwsCrtResultVoid = AwsCrtResult<void>;

        template <typename R> AwsCrtResult<R> MakeLastErrorResult()
        {
            int error = aws_last_error();
            if (error == AWS_ERROR_SUCCESS)
            {
                error = AWS_ERROR_UNKNOWN;
            }

            return AwsCrtResult<R>(error);
        }
    } // namespace Crt
} // namespace Aws
