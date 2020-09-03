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
/**
 *  @class basic_string_view <string_view>
 *  @brief  A non-owning reference to a string.
 *
 *  @ingroup strings
 *  @ingroup sequences
 *
 *  @tparam _CharT  Type of character
 *  @tparam _Traits  Traits for character type, defaults to
 *                   char_traits<_CharT>.
 *
 *  A basic_string_view looks like this:
 *
 *  @code
 *    _CharT*    _M_str
 *    size_t     _M_len
 *  @endcode
 */
template <typename _CharT, typename _Traits = std::char_traits<_CharT>> class basic_string_view
{
  public:
    // types
    using traits_type = _Traits;
    using value_type = _CharT;
    using pointer = value_type *;
    using const_pointer = const value_type *;
    using reference = value_type &;
    using const_reference = const value_type &;
    using const_iterator = const value_type *;
    using iterator = const_iterator;
    using const_reverse_iterator = std::reverse_iterator<const_iterator>;
    using reverse_iterator = const_reverse_iterator;
    using size_type = size_t;
    using difference_type = ptrdiff_t;
    static constexpr size_type npos = static_cast<size_type>(-1);

    // [string.view.cons], construction and assignment

    constexpr basic_string_view() noexcept : _M_len{0}, _M_str{nullptr} {}

    constexpr basic_string_view(const basic_string_view &) noexcept = default;

    constexpr basic_string_view(const _CharT *__str) noexcept : _M_len{traits_type::length(__str)}, _M_str{__str} {}

    constexpr basic_string_view(const _CharT *__str, size_type __len) noexcept : _M_len{__len}, _M_str{__str} {}

    basic_string_view &operator=(const basic_string_view &) noexcept = default;

    // [string.view.iterators], iterator support

    constexpr const_iterator begin() const noexcept { return this->_M_str; }

    constexpr const_iterator end() const noexcept { return this->_M_str + this->_M_len; }

    constexpr const_iterator cbegin() const noexcept { return this->_M_str; }

    constexpr const_iterator cend() const noexcept { return this->_M_str + this->_M_len; }

    constexpr const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(this->end()); }

    constexpr const_reverse_iterator rend() const noexcept { return const_reverse_iterator(this->begin()); }

    constexpr const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(this->end()); }

    constexpr const_reverse_iterator crend() const noexcept { return const_reverse_iterator(this->begin()); }

    // [string.view.capacity], capacity

    constexpr size_type size() const noexcept { return this->_M_len; }

    constexpr size_type length() const noexcept { return _M_len; }

    constexpr size_type max_size() const noexcept
    {
        return (npos - sizeof(size_type) - sizeof(void *)) / sizeof(value_type) / 4;
    }

    constexpr bool empty() const noexcept { return this->_M_len == 0; }

    // [string.view.access], element access

    const_reference operator[](size_type __pos) const noexcept
    {
        assert(__pos < _M_len);
        return *(this->_M_str + __pos);
    }

    const_reference at(size_type __pos) const
    {
        assert(__pos < _M_len);
        return *(this->_M_str + __pos);
    }

    const_reference front() const noexcept
    {
        assert(_M_len > 0);
        return *this->_M_str;
    }

    const_reference back() const noexcept
    {
        assert(_M_len > 0);
        return *(this->_M_str + this->_M_len - 1);
    }

    constexpr const_pointer data() const noexcept { return this->_M_str; }

    // [string.view.modifiers], modifiers:

    void remove_prefix(size_type __n) noexcept
    {
        assert(this->_M_len >= __n);
        this->_M_str += __n;
        this->_M_len -= __n;
    }

    void remove_suffix(size_type __n) noexcept { this->_M_len -= __n; }

    void swap(basic_string_view &__sv) noexcept
    {
        auto __tmp = *this;
        *this = __sv;
        __sv = __tmp;
    }

    // [string.view.ops], string operations:

    size_type copy(_CharT *__str, size_type __n, size_type __pos = 0) const
    {
        assert(__pos <= size());
        const size_type __rlen = (std::min)(__n, _M_len - __pos);
        traits_type::copy(__str, data() + __pos, __rlen);
        return __rlen;
    }

    basic_string_view substr(size_type __pos = 0, size_type __n = npos) const noexcept(false)
    {
        assert(__pos <= size());
        const size_type __rlen = (std::min)(__n, _M_len - __pos);
        return basic_string_view{_M_str + __pos, __rlen};
    }

    int compare(basic_string_view __str) const noexcept
    {
        const size_type __rlen = (std::min)(this->_M_len, __str._M_len);
        int __ret = traits_type::compare(this->_M_str, __str._M_str, __rlen);
        if (__ret == 0)
            __ret = _S_compare(this->_M_len, __str._M_len);
        return __ret;
    }

    constexpr int compare(size_type __pos1, size_type __n1, basic_string_view __str) const
    {
        return this->substr(__pos1, __n1).compare(__str);
    }

    constexpr int compare(size_type __pos1, size_type __n1, basic_string_view __str, size_type __pos2, size_type __n2)
        const
    {
        return this->substr(__pos1, __n1).compare(__str.substr(__pos2, __n2));
    }

    constexpr int compare(const _CharT *__str) const noexcept { return this->compare(basic_string_view{__str}); }

    constexpr int compare(size_type __pos1, size_type __n1, const _CharT *__str) const
    {
        return this->substr(__pos1, __n1).compare(basic_string_view{__str});
    }

    constexpr int compare(size_type __pos1, size_type __n1, const _CharT *__str, size_type __n2) const noexcept(false)
    {
        return this->substr(__pos1, __n1).compare(basic_string_view(__str, __n2));
    }

    constexpr bool starts_with(basic_string_view __x) const noexcept { return this->substr(0, __x.size()) == __x; }

    constexpr bool starts_with(_CharT __x) const noexcept
    {
        return !this->empty() && traits_type::eq(this->front(), __x);
    }

    constexpr bool starts_with(const _CharT *__x) const noexcept { return this->starts_with(basic_string_view(__x)); }

    constexpr bool ends_with(basic_string_view __x) const noexcept
    {
        return this->size() >= __x.size() && this->compare(this->size() - __x.size(), npos, __x) == 0;
    }

    constexpr bool ends_with(_CharT __x) const noexcept { return !this->empty() && traits_type::eq(this->back(), __x); }

    constexpr bool ends_with(const _CharT *__x) const noexcept { return this->ends_with(basic_string_view(__x)); }

    // [string.view.find], searching

    constexpr size_type find(basic_string_view __str, size_type __pos = 0) const noexcept
    {
        return this->find(__str._M_str, __pos, __str._M_len);
    }

    size_type find(_CharT __c, size_type __pos = 0) const noexcept
    {
        if (__pos >= _M_len)
        {
            return npos;
        }
        const _CharT *__r = _Traits::find(_M_str + __pos, _M_len - __pos, __c);
        if (__r == 0)
        {
            return npos;
        }
        return static_cast<size_type>(__r - _M_str);
    }

    size_type find(const _CharT *__str, size_type __pos, size_type __n) const noexcept
    {
        if (__n && !__str)
        {
            return npos;
        }

        if (__pos > _M_len)
        {
            return npos;
        }
        if (__n == 0)
        { // There is nothing to search, just return __pos.
            return __pos;
        }

        const _CharT *__r = __search_substring(_M_str + __pos, _M_str + _M_len, __str, __str + __n);

        if (__r == _M_str + _M_len)
            return npos;
        return static_cast<size_type>(__r - _M_str);
    }

    constexpr size_type find(const _CharT *__str, size_type __pos = 0) const noexcept
    {
        return this->find(__str, __pos, traits_type::length(__str));
    }

    size_type rfind(basic_string_view __str, size_type __pos = npos) const noexcept
    {
        if (__str._M_len && !__str._M_str)
        {
            return npos;
        }
        return this->rfind(__str._M_str, __pos, __str._M_len);
    }

    size_type rfind(_CharT __c, size_type __pos = npos) const noexcept
    {
        if (_M_len < 1)
            return npos;

        if (__pos < _M_len)
        {
            ++__pos;
        }
        else
        {
            __pos = _M_len;
        }
        for (const _CharT *__ps = _M_str + __pos; __ps != _M_str;)
        {
            if (_Traits::eq(*--__ps, __c))
            {
                return static_cast<size_type>(__ps - _M_str);
            }
        }
        return npos;
    }

    size_type rfind(const _CharT *__str, size_type __pos, size_type __n) const noexcept
    {
        if (__n && !__str)
        {
            return npos;
        }

        __pos = (std::min)(__pos, _M_len);
        if (__n < _M_len - __pos)
        {
            __pos += __n;
        }
        else
        {
            __pos = _M_len;
        }
        const _CharT *__r = __find_end(_M_str, _M_str + __pos, __str, __str + __n);
        if (__n > 0 && __r == _M_str + __pos)
        {
            return npos;
        }
        return static_cast<size_type>(__r - _M_str);
    }

    constexpr size_type rfind(const _CharT *__str, size_type __pos = npos) const noexcept
    {
        return this->rfind(__str, __pos, traits_type::length(__str));
    }

    constexpr size_type find_first_of(basic_string_view __str, size_type __pos = 0) const noexcept
    {
        return this->find_first_of(__str._M_str, __pos, __str._M_len);
    }

    constexpr size_type find_first_of(_CharT __c, size_type __pos = 0) const noexcept { return this->find(__c, __pos); }

    size_type find_first_of(const _CharT *__str, size_type __pos, size_type __n) const noexcept
    {
        if (__pos >= _M_len || !__n || !__str)
            return npos;

        const _CharT *__r = __find_first_of_ce(_M_str + __pos, _M_str + _M_len, __str, __str + __n);

        if (__r == _M_str + _M_len)
            return npos;

        return static_cast<size_type>(__r - _M_str);
    }

    constexpr size_type find_first_of(const _CharT *__str, size_type __pos = 0) const noexcept
    {
        return this->find_first_of(__str, __pos, traits_type::length(__str));
    }

    constexpr size_type find_last_of(basic_string_view __str, size_type __pos = npos) const noexcept
    {
        return this->find_last_of(__str._M_str, __pos, __str._M_len);
    }

    constexpr size_type find_last_of(_CharT __c, size_type __pos = npos) const noexcept
    {
        return this->rfind(__c, __pos);
    }

    size_type find_last_of(const _CharT *__str, size_type __pos, size_type __n) const noexcept
    {
        if (!__n || __str == nullptr)
            return npos;

        if (__pos < _M_len)
        {
            ++__pos;
        }
        else
        {
            __pos = _M_len;
        }
        for (const _CharT *__ps = _M_str + __pos; __ps != _M_str;)
        {
            const _CharT *__r = _Traits::find(__str, __n, *--__ps);
            if (__r)
            {
                return static_cast<size_type>(__ps - _M_str);
            }
        }

        return npos;
    }

    constexpr size_type find_last_of(const _CharT *__str, size_type __pos = npos) const noexcept
    {
        return this->find_last_of(__str, __pos, traits_type::length(__str));
    }

    size_type find_first_not_of(basic_string_view __str, size_type __pos = 0) const noexcept
    {
        if (__str._M_len && !__str._M_str)
        {
            return npos;
        }
        return this->find_first_not_of(__str._M_str, __pos, __str._M_len);
    }

    size_type find_first_not_of(_CharT __c, size_type __pos = 0) const noexcept
    {
        if (_M_str == nullptr || __pos >= _M_len)
            return npos;

        const _CharT *__pe = _M_str + _M_len;
        for (const _CharT *__ps = _M_str + __pos; __ps != __pe; ++__ps)
        {
            if (!_Traits::eq(*__ps, __c))
                return static_cast<size_type>(__ps - _M_str);
        }

        return npos;
    }

    size_type find_first_not_of(const _CharT *__str, size_type __pos, size_type __n) const noexcept
    {
        if (__n && __str == nullptr)
            return npos;
        if (_M_str == nullptr || __pos >= _M_len)
            return npos;

        const _CharT *__pe = _M_str + _M_len;
        for (const _CharT *__ps = _M_str + __pos; __ps != __pe; ++__ps)
        {
            if (_Traits::find(__str, __n, *__ps) == 0)
                return static_cast<size_type>(__ps - _M_str);
        }

        return npos;
    }

    constexpr size_type find_first_not_of(const _CharT *__str, size_type __pos = 0) const noexcept
    {
        return this->find_first_not_of(__str, __pos, traits_type::length(__str));
    }

    size_type find_last_not_of(basic_string_view __str, size_type __pos = npos) const noexcept
    {
        if (__str._M_len && !__str._M_str)
        {
            return npos;
        }
        return this->find_last_not_of(__str._M_str, __pos, __str._M_len);
    }

    size_type find_last_not_of(_CharT __c, size_type __pos = npos) const noexcept
    {
        if (__pos < _M_len)
        {
            ++__pos;
        }
        else
        {
            __pos = _M_len;
        }

        for (const _CharT *__ps = _M_str + __pos; __ps != _M_str;)
        {
            if (!_Traits::eq(*--__ps, __c))
                return static_cast<size_type>(__ps - _M_str);
        }
        return npos;
    }

    size_type find_last_not_of(const _CharT *__str, size_type __pos, size_type __n) const noexcept
    {
        if (__n && !__str)
            return npos;

        if (__pos < _M_len)
        {
            ++__pos;
        }
        else
        {
            __pos = _M_len;
        }

        for (const _CharT *__ps = _M_str + __pos; __ps != _M_str;)
        {
            if (_Traits::find(__str, __n, *--__ps) == 0)
                return static_cast<size_type>(__ps - _M_str);
        }
        return npos;
    }

    constexpr size_type find_last_not_of(const _CharT *__str, size_type __pos = npos) const noexcept
    {
        return this->find_last_not_of(__str, __pos, traits_type::length(__str));
    }

  private:
    static int _S_compare(size_type __n1, size_type __n2) noexcept
    {
        const difference_type __diff = __n1 - __n2;
        if (__diff > std::numeric_limits<int>::max())
            return std::numeric_limits<int>::max();
        if (__diff < std::numeric_limits<int>::min())
            return std::numeric_limits<int>::min();
        return static_cast<int>(__diff);
    }

    inline const _CharT *__search_substring(
        const _CharT *__first1,
        const _CharT *__last1,
        const _CharT *__first2,
        const _CharT *__last2) const
    {
        // Take advantage of knowing source and pattern lengths.
        // Stop short when source is smaller than pattern.
        const ptrdiff_t __len2 = __last2 - __first2;
        if (__len2 == 0)
            return __first1;

        ptrdiff_t __len1 = __last1 - __first1;
        if (__len1 < __len2)
            return __last1;

        // First element of __first2 is loop invariant.
        _CharT __f2 = *__first2;
        while (true)
        {
            __len1 = __last1 - __first1;
            // Check whether __first1 still has at least __len2 bytes.
            if (__len1 < __len2)
                return __last1;

            // Find __f2 the first byte matching in __first1.
            __first1 = _Traits::find(__first1, __len1 - __len2 + 1, __f2);
            if (__first1 == 0)
                return __last1;

            // It is faster to compare from the first byte of __first1 even if we
            // already know that it matches the first byte of __first2: this is because
            // __first2 is most likely aligned, as it is user's "pattern" string, and
            // __first1 + 1 is most likely not aligned, as the match is in the middle of
            // the string.
            if (_Traits::compare(__first1, __first2, __len2) == 0)
                return __first1;

            ++__first1;
        }
    }

    const _CharT *__find_end(
        const _CharT *__first1,
        const _CharT *__last1,
        const _CharT *__first2,
        const _CharT *__last2) const
    {
        // modeled after search algorithm
        const _CharT *__r = __last1; // __last1 is the "default" answer
        if (__first2 == __last2)
            return __r;
        while (true)
        {
            while (true)
            {
                if (__first1 == __last1) // if source exhausted return last correct answer
                    return __r;          //    (or __last1 if never found)
                if (_Traits::eq(*__first1, *__first2))
                    break;
                ++__first1;
            }
            // *__first1 matches *__first2, now match elements after here
            const _CharT *__m1 = __first1;
            const _CharT *__m2 = __first2;
            while (true)
            {
                if (++__m2 == __last2)
                { // Pattern exhausted, record answer and search for another one
                    __r = __first1;
                    ++__first1;
                    break;
                }
                if (++__m1 == __last1) // Source exhausted, return last answer
                    return __r;
                if (!_Traits::eq(*__m1, *__m2)) // mismatch, restart with a new __first
                {
                    ++__first1;
                    break;
                } // else there is a match, check next elements
            }
        }
    }

    const _CharT *__find_first_of_ce(
        const _CharT *__first1,
        const _CharT *__last1,
        const _CharT *__first2,
        const _CharT *__last2) const
    {
        for (; __first1 != __last1; ++__first1)
            for (const _CharT *__j = __first2; __j != __last2; ++__j)
                if (_Traits::eq(*__first1, *__j))
                    return __first1;
        return __last1;
    }

    size_type _M_len;
    const _CharT *_M_str;
};

// [string.view.comparison]
// operator ==
template <class _CharT, class _Traits>
bool operator==(basic_string_view<_CharT, _Traits> __lhs, basic_string_view<_CharT, _Traits> __rhs) noexcept
{
    if (__lhs.size() != __rhs.size())
        return false;
    return __lhs.compare(__rhs) == 0;
}

template <class _CharT, class _Traits>
bool operator==(
    basic_string_view<_CharT, _Traits> __lhs,
    typename std::common_type<basic_string_view<_CharT, _Traits>>::type __rhs) noexcept
{
    if (__lhs.size() != __rhs.size())
        return false;
    return __lhs.compare(__rhs) == 0;
}

template <class _CharT, class _Traits>
bool operator==(
    typename std::common_type<basic_string_view<_CharT, _Traits>>::type __lhs,
    basic_string_view<_CharT, _Traits> __rhs) noexcept
{
    if (__lhs.size() != __rhs.size())
        return false;
    return __lhs.compare(__rhs) == 0;
}

// operator !=
template <class _CharT, class _Traits>
bool operator!=(basic_string_view<_CharT, _Traits> __lhs, basic_string_view<_CharT, _Traits> __rhs) noexcept
{
    if (__lhs.size() != __rhs.size())
        return true;
    return __lhs.compare(__rhs) != 0;
}

template <class _CharT, class _Traits>
bool operator!=(
    basic_string_view<_CharT, _Traits> __lhs,
    typename std::common_type<basic_string_view<_CharT, _Traits>>::type __rhs) noexcept
{
    if (__lhs.size() != __rhs.size())
        return true;
    return __lhs.compare(__rhs) != 0;
}

template <class _CharT, class _Traits>
bool operator!=(
    typename std::common_type<basic_string_view<_CharT, _Traits>>::type __lhs,
    basic_string_view<_CharT, _Traits> __rhs) noexcept
{
    if (__lhs.size() != __rhs.size())
        return true;
    return __lhs.compare(__rhs) != 0;
}

// operator <
template <class _CharT, class _Traits>
bool operator<(basic_string_view<_CharT, _Traits> __lhs, basic_string_view<_CharT, _Traits> __rhs) noexcept
{
    return __lhs.compare(__rhs) < 0;
}

template <class _CharT, class _Traits>
constexpr bool operator<(
    basic_string_view<_CharT, _Traits> __lhs,
    typename std::common_type<basic_string_view<_CharT, _Traits>>::type __rhs) noexcept
{
    return __lhs.compare(__rhs) < 0;
}

template <class _CharT, class _Traits>
constexpr bool operator<(
    typename std::common_type<basic_string_view<_CharT, _Traits>>::type __lhs,
    basic_string_view<_CharT, _Traits> __rhs) noexcept
{
    return __lhs.compare(__rhs) < 0;
}

// operator >
template <class _CharT, class _Traits>
constexpr bool operator>(basic_string_view<_CharT, _Traits> __lhs, basic_string_view<_CharT, _Traits> __rhs) noexcept
{
    return __lhs.compare(__rhs) > 0;
}

template <class _CharT, class _Traits>
constexpr bool operator>(
    basic_string_view<_CharT, _Traits> __lhs,
    typename std::common_type<basic_string_view<_CharT, _Traits>>::type __rhs) noexcept
{
    return __lhs.compare(__rhs) > 0;
}

template <class _CharT, class _Traits>
constexpr bool operator>(
    typename std::common_type<basic_string_view<_CharT, _Traits>>::type __lhs,
    basic_string_view<_CharT, _Traits> __rhs) noexcept
{
    return __lhs.compare(__rhs) > 0;
}

// operator <=
template <class _CharT, class _Traits>
constexpr bool operator<=(basic_string_view<_CharT, _Traits> __lhs, basic_string_view<_CharT, _Traits> __rhs) noexcept
{
    return __lhs.compare(__rhs) <= 0;
}

template <class _CharT, class _Traits>
constexpr bool operator<=(
    basic_string_view<_CharT, _Traits> __lhs,
    typename std::common_type<basic_string_view<_CharT, _Traits>>::type __rhs) noexcept
{
    return __lhs.compare(__rhs) <= 0;
}

template <class _CharT, class _Traits>
constexpr bool operator<=(
    typename std::common_type<basic_string_view<_CharT, _Traits>>::type __lhs,
    basic_string_view<_CharT, _Traits> __rhs) noexcept
{
    return __lhs.compare(__rhs) <= 0;
}

// operator >=
template <class _CharT, class _Traits>
constexpr bool operator>=(basic_string_view<_CharT, _Traits> __lhs, basic_string_view<_CharT, _Traits> __rhs) noexcept
{
    return __lhs.compare(__rhs) >= 0;
}

template <class _CharT, class _Traits>
constexpr bool operator>=(
    basic_string_view<_CharT, _Traits> __lhs,
    typename std::common_type<basic_string_view<_CharT, _Traits>>::type __rhs) noexcept
{
    return __lhs.compare(__rhs) >= 0;
}

template <class _CharT, class _Traits>
constexpr bool operator>=(
    typename std::common_type<basic_string_view<_CharT, _Traits>>::type __lhs,
    basic_string_view<_CharT, _Traits> __rhs) noexcept
{
    return __lhs.compare(__rhs) >= 0;
}

typedef basic_string_view<char> string_view;
typedef basic_string_view<char16_t> u16string_view;
typedef basic_string_view<char32_t> u32string_view;
typedef basic_string_view<wchar_t> wstring_view;

// This function utility is copied from Windows. Clang implementation is a bit complex.
#    if defined(_WIN64)
constexpr size_t _FNV_offset_basis = 14695981039346656037ULL;
constexpr size_t _FNV_prime = 1099511628211ULL;
#    else  // defined(_WIN64)
constexpr size_t _FNV_offset_basis = 2166136261U;
constexpr size_t _FNV_prime = 16777619U;
#    endif // defined(_WIN64)

inline size_t _Fnv1a_append_bytes(size_t _Val, const unsigned char *const _First, const size_t _Count) noexcept
{
    for (size_t _Idx = 0; _Idx < _Count; ++_Idx)
    {
        _Val ^= static_cast<size_t>(_First[_Idx]);
        _Val *= _FNV_prime;
    }

    return _Val;
}

// [string.view.hash]
namespace std
{
    template <class _CharT, class _Traits>
    struct hash<basic_string_view<_CharT, _Traits>>
        : public std::unary_function<basic_string_view<_CharT, _Traits>, size_t>
    {
        size_t operator()(const basic_string_view<_CharT, _Traits> __val) const noexcept;
    };

    template <class _CharT, class _Traits>
    size_t hash<basic_string_view<_CharT, _Traits>>::operator()(const basic_string_view<_CharT, _Traits> __val) const
        noexcept
    {
        return _Fnv1a_append_bytes(
            _FNV_offset_basis, reinterpret_cast<const unsigned char *>(__val.data()), __val.size() * sizeof(_CharT));
    }

} // namespace std

inline namespace literals
{
    inline namespace string_view_literals
    {
        inline basic_string_view<char> operator"" _sv(const char *__str, size_t __len) noexcept
        {
            return basic_string_view<char>(__str, __len);
        }

        inline basic_string_view<wchar_t> operator"" _sv(const wchar_t *__str, size_t __len) noexcept
        {
            return basic_string_view<wchar_t>(__str, __len);
        }

        inline basic_string_view<char16_t> operator"" _sv(const char16_t *__str, size_t __len) noexcept
        {
            return basic_string_view<char16_t>(__str, __len);
        }

        inline basic_string_view<char32_t> operator"" _sv(const char32_t *__str, size_t __len) noexcept
        {
            return basic_string_view<char32_t>(__str, __len);
        }
    } // namespace string_view_literals

} // namespace literals
#endif

namespace Aws
{
    namespace Crt
    {
#if __cplusplus >= 201703L
        using StringView = std::string_view;
#else
        using StringView = string_view;
#endif
    } // namespace Crt
} // namespace Aws
