#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */
#if __cplusplus >= 201703L
#    include <optional>
#else
#    include <cstdint>
#    include <type_traits>
#    include <utility>
#endif

namespace Aws
{
    namespace Crt
    {
#if __cplusplus >= 201703L
        template <typename T> using Optional = std::optional<T>;
#else
        template <typename T> class Optional
        {
          public:
            Optional() : m_value(nullptr) {}
            Optional(const T &val)
            {
                new (&m_storage) T(val);
                m_value = reinterpret_cast<T *>(&m_storage);
            }

            Optional(T &&val)
            {
                new (&m_storage) T(std::forward<T>(val));
                m_value = reinterpret_cast<T *>(&m_storage);
            }

            ~Optional()
            {
                if (m_value)
                {
                    m_value->~T();
                }
            }

            template <typename U = T> Optional &operator=(U &&u)
            {
                if (m_value)
                {
                    *m_value = std::forward<U>(u);
                    return *this;
                }

                new (&m_storage) T(std::forward<U>(u));
                m_value = reinterpret_cast<T *>(&m_storage);

                return *this;
            }

            Optional(const Optional<T> &other)
            {
                if (other.m_value)
                {
                    new (&m_storage) T(*other.m_value);
                    m_value = reinterpret_cast<T *>(&m_storage);
                }
                else
                {
                    m_value = nullptr;
                }
            }

            Optional(Optional<T> &&other)
            {
                if (other.m_value)
                {
                    new (&m_storage) T(std::forward<T>(*other.m_value));
                    m_value = reinterpret_cast<T *>(&m_storage);
                }
                else
                {
                    m_value = nullptr;
                }
            }

            Optional &operator=(const Optional &other)
            {
                if (this == &other)
                {
                    return *this;
                }

                if (m_value)
                {
                    if (other.m_value)
                    {
                        *m_value = *other.m_value;
                    }
                    else
                    {
                        m_value->~T();
                        m_value = nullptr;
                    }

                    return *this;
                }

                if (other.m_value)
                {
                    new (&m_storage) T(*other.m_value);
                    m_value = reinterpret_cast<T *>(&m_storage);
                }

                return *this;
            }

            template <typename U = T> Optional<T> &operator=(const Optional<U> &other)
            {
                if (this == &other)
                {
                    return *this;
                }

                if (m_value)
                {
                    if (other.m_value)
                    {
                        *m_value = *other.m_value;
                    }
                    else
                    {
                        m_value->~T();
                        m_value = nullptr;
                    }

                    return *this;
                }

                if (other.m_value)
                {
                    new (&m_storage) T(*other.m_value);
                    m_value = reinterpret_cast<T *>(&m_storage);
                }

                return *this;
            }

            template <typename U = T> Optional<T> &operator=(Optional<U> &&other)
            {
                if (this == &other)
                {
                    return *this;
                }

                if (m_value)
                {
                    if (other.m_value)
                    {
                        *m_value = std::forward<U>(*other.m_value);
                    }
                    else
                    {
                        m_value->~T();
                        m_value = nullptr;
                    }

                    return *this;
                }

                if (other.m_value)
                {
                    new (&m_storage) T(std::forward<U>(*other.m_value));
                    m_value = reinterpret_cast<T *>(&m_storage);
                }

                return *this;
            }

            const T *operator->() const { return m_value; }
            T *operator->() { return m_value; }
            const T &operator*() const & { return *m_value; }
            T &operator*() & { return *m_value; }
            const T &&operator*() const && { return std::move(*m_value); }
            T &&operator*() && { return std::move(*m_value); }

            explicit operator bool() const noexcept { return m_value != nullptr; }
            bool has_value() const noexcept { return m_value != nullptr; }

            T &value() & { return *m_value; }
            const T &value() const & { return *m_value; }

            T &&value() && { return std::move(*m_value); }
            const T &&value() const && { return std::move(*m_value); }

          private:
            typename std::aligned_storage<sizeof(T)>::type m_storage;
            T *m_value;
        };
#endif
    } // namespace Crt
} // namespace Aws