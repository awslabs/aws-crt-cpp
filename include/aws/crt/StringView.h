#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#if __cplusplus >= 201703L
#    include <string_view>
#else
#    include <algorithm>
#    include <cassert>
#    include <iterator>
#    include <type_traits>
#endif

namespace Aws
{
    namespace Crt
    {
#if __cplusplus >= 201703L
        using StringView = std::string_view;
#else
        class StringView
        {
          public:
            typedef char *pointer;
            typedef const char *const_pointer;
            typedef char &reference;
            typedef const char &const_reference;
            typedef const_pointer const_iterator;
            typedef const_iterator iterator;
            typedef std::reverse_iterator<const_iterator> const_reverse_iterator;
            typedef const_reverse_iterator reverse_iterator;
            static constexpr const size_t npos = static_cast<size_t>(-1);

            StringView() noexcept : m_size(0), m_data(nullptr) {}
            StringView(const StringView &other) noexcept = default;
            StringView(const char *s, size_t size) noexcept : m_size(size), m_data(s) {}
            StringView(const char *s) noexcept : m_size(std::char_traits<char>::length(s)), m_data(s) {}

            StringView &operator=(StringView &other) noexcept = default;

            const_iterator begin() const noexcept { return cbegin(); }

            const_iterator end() const noexcept { return cend(); }

            const_iterator cbegin() const noexcept { return m_data; }

            const_iterator cend() const noexcept { return m_data + m_size; }

            const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(cend()); }

            const_reverse_iterator rend() const noexcept { return const_reverse_iterator(cbegin()); }

            const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }

            const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

            size_t size() const noexcept { return m_size; }

            size_t length() const noexcept { return m_size; }

            size_t max_size() const noexcept { return std::numeric_limits<size_t>::max(); }

            bool empty() const noexcept { return m_size == 0; }

            const_reference operator[](size_t pos) const noexcept
            {
                assert(pos < m_size);
                return m_data[pos];
            }

            const_reference at(size_t pos) const noexcept
            {
                assert(pos < m_size);
                return m_data[pos];
            }

            const_reference front() const noexcept
            {
                assert(m_size != 0);
                return m_data[0];
            }

            const_reference back() const noexcept
            {
                assert(m_size != 0);
                return m_data[m_size - 1];
            }

            const_pointer data() const noexcept { return m_data; }

            void remove_prefix(size_t n) noexcept
            {
                assert(n <= m_size);
                m_data += n;
                m_size -= n;
            }

            void remove_suffix(size_t n) noexcept
            {
                assert(n <= m_size);
                m_size -= n;
            }

            void swap(StringView &other) noexcept
            {
                const char *p = m_data;
                m_data = other.m_data;
                other.m_data = p;

                size_t sz = m_size;
                m_size = other.m_size;
                other.m_size = sz;
            }

            size_t copy(char *s, size_t n, size_t pos = 0) const
            {
                assert(pos <= m_size);
                size_t copyLen = std::min(n, m_size - pos);
                std::char_traits<char>::copy(s, m_data + pos, copyLen);
                return copyLen;
            }

            StringView substr(size_t pos = 0, size_t n = npos) const
            {
                assert(pos <= m_size);
                return StringView(m_data + pos, std::min(n, m_size - pos));
            }

          private:
            size_t m_size;
            const char *m_data;
        };
#endif
    } // namespace Crt
} // namespace Aws
