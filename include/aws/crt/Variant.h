#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/common/assert.h>

#include <algorithm>
#include <type_traits>
#include <utility>

namespace Aws
{
    namespace Crt
    {
        namespace VariantDetail
        {
            template <typename T> constexpr const T &ConstExprMax(const T &a, const T &b) { return (a < b) ? b : a; }

            namespace ParameterPackSize
            {
                // Returns a max of sizeof(T) over all T in a template parameter pack
                template <typename Last> constexpr std::size_t GetMaxSizeOf(std::size_t curMax = 0)
                {
                    return ConstExprMax(curMax, sizeof(Last));
                }

                template <typename First, typename Second, typename... Rest>
                constexpr std::size_t GetMaxSizeOf(std::size_t curMax = 0)
                {
                    return ConstExprMax(curMax, GetMaxSizeOf<Second, Rest...>(ConstExprMax(curMax, sizeof(First))));
                }

                // some old gcc versions does not work with alignas(Ts..)
                template <typename Last> constexpr std::size_t AlignAsPack(std::size_t curMax = 0)
                {
                    return ConstExprMax(curMax, alignof(Last));
                }

                template <typename First, typename Second, typename... Rest>
                constexpr std::size_t AlignAsPack(std::size_t curMax = 0)
                {
                    return ConstExprMax(curMax, AlignAsPack<Second, Rest...>(ConstExprMax(curMax, alignof(First))));
                }
            } // namespace ParameterPackSize

            namespace Index
            {
                using VariantIndex = short;

                template <typename T, typename Last> constexpr VariantIndex GetIndexOf(VariantIndex curIndex = -1)
                {
                    return std::is_same<T, Last>::value ? curIndex : -1;
                }

                template <typename T, typename First, typename Second, typename... Rest>
                constexpr VariantIndex GetIndexOf(VariantIndex curIndex = 0)
                {
                    return std::is_same<T, First>::value ? curIndex : GetIndexOf<T, Second, Rest...>(++curIndex);
                }
            } // namespace Index

            namespace Checker
            {
                // Returns True if the template parameter pack Ts has a type T, i.e. ContainsType<T, Ts>() == true if T
                // is in the list of Ts
                template <typename T, typename Last> constexpr bool ContainsType()
                {
                    return std::is_same<T, Last>::value;
                }

                template <typename T, typename First, typename Second, typename... Rest> constexpr bool ContainsType()
                {
                    return std::is_same<T, First>::value || ContainsType<T, Second, Rest...>();
                }

                // a case when the template parameter pack is empty (i.e. Variant<>)
                template <typename T> constexpr bool ContainsType() { return false; }

                template <typename T, typename... Ts> struct HasType
                {
                    static const bool value = ContainsType<T, Ts...>();
                };
            } // namespace Checker
        }     // namespace VariantDetail

        template <std::size_t Index, typename... Ts> class VariantAlternative;

        template <typename T> struct VariantInPlaceInitT
        {
            explicit VariantInPlaceInitT() = default;
        };

        /**
         * Custom implementation of a Variant type. std::variant requires C++17
         * @tparam Ts types of the variant value
         */
        template <typename... Ts> class Variant
        {
          public:
            static constexpr std::size_t AlternativeCount = sizeof...(Ts);

            template <std::size_t Index> using ThisVariantAlternative = VariantAlternative<Index, Ts...>;

            using ThisVariantT = Variant<Ts...>;
            template <typename OtherT>
            using EnableIfOtherIsSameVariantType =
                typename std::enable_if<std::is_same<typename std::decay<OtherT>, ThisVariantT>::value, int>::type;

            template <typename OtherT>
            using EnableIfOtherIsThisVariantAlternative = typename std::
                enable_if<VariantDetail::Checker::HasType<typename std::decay<OtherT>::type, Ts...>::value, int>::type;

            Variant()
            {
                using FirstAlternative = typename ThisVariantAlternative<0>::type;
                new (m_storage) FirstAlternative();
                m_index = 0;
            }

            Variant(const Variant &other)
            {
                auto otherIdx = other.m_index;
                if (otherIdx != -1)
                {
                    CopyConstructUtil<0, Ts...>(other);
                }
            }

            Variant(Variant &&other)
            {
                auto otherIdx = other.m_index;
                if (otherIdx != -1)
                {
                    MoveConstructUtil<0, Ts...>(std::move(other));
                }
            }

            template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1> Variant(const T &val)
            {
                static_assert(
                    VariantDetail::Checker::HasType<typename std::decay<T>::type, Ts...>::value,
                    "This variant does not have such alternative T.");
                static_assert(
                    sizeof(T) >= STORAGE_SIZE,
                    "Attempting to instantiate a Variant with a type bigger than all alternatives.");

                using PlainT = typename std::decay<T>::type;
                new (m_storage) PlainT(val);
                m_index = VariantDetail::Index::GetIndexOf<T, Ts...>();
                AWS_ASSERT(m_index != -1);
            }

            template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1> Variant(T &&val)
            {
                static_assert(
                    VariantDetail::Checker::HasType<typename std::decay<T>::type, Ts...>::value,
                    "This variant does not have such alternative T.");
                static_assert(
                    sizeof(T) <= STORAGE_SIZE,
                    "Attempting to instantiate a Variant with a type bigger than all alternatives.");

                using PlainT = typename std::decay<T>::type;
                new (m_storage) PlainT(std::forward<T>(val));
                m_index = VariantDetail::Index::GetIndexOf<T, Ts...>();
                AWS_ASSERT(m_index != -1);
            }

            // An overload to initialize with an Alternative T in-place
            template <typename T, typename... Args> explicit Variant(VariantInPlaceInitT<T>, Args &&...args)
            {
                static_assert(
                    VariantDetail::Checker::HasType<typename std::decay<T>::type, Ts...>::value,
                    "This variant does not have such alternative T.");
                static_assert(
                    sizeof(T) <= STORAGE_SIZE,
                    "Attempting to instantiate a Variant with a type bigger than all alternatives.");

                using PlainT = typename std::decay<T>::type;
                new (m_storage) PlainT(std::forward<Args>(args)...);
                m_index = VariantDetail::Index::GetIndexOf<T, Ts...>();
                AWS_ASSERT(m_index != -1);
            }

            Variant &operator=(const Variant &other)
            {
                if (this != &other)
                {
                    if (m_index != other.m_index)
                    {
                        Destroy();
                        CopyConstructUtil<0, Ts...>(other);
                    }
                    else
                    {
                        CopyAssignUtil<0, Ts...>(other);
                    }
                }
                return *this;
            }

            Variant &operator=(Variant &&other)
            {
                if (this != &other)
                {
                    if (m_index != other.m_index)
                    {
                        Destroy();
                        MoveConstructUtil<0, Ts...>(std::move(other));
                    }
                    else
                    {
                        MoveAssignUtil<0, Ts...>(std::move(other));
                    };
                }
                return *this;
            }

            /* emplace */
            template <typename T, typename... Args, EnableIfOtherIsThisVariantAlternative<T> = 1>
            T &emplace(Args &&...args)
            {
                static_assert(
                    VariantDetail::Checker::HasType<typename std::decay<T>::type, Ts...>::value,
                    "This variant does not have such alternative T.");
                static_assert(
                    sizeof(T) <= STORAGE_SIZE,
                    "Attempting to instantiate a Variant with a type bigger than all alternatives.");

                Destroy();

                using PlainT = typename std::decay<T>::type;
                new (m_storage) PlainT(std::forward<Args>(args)...);
                m_index = VariantDetail::Index::GetIndexOf<T, Ts...>();
                AWS_ASSERT(m_index != -1);

                T *value = reinterpret_cast<T *>(m_storage);
                return *value;
            }

            template <std::size_t Index, typename... Args>
            auto emplace(Args &&...args) -> typename ThisVariantAlternative<Index>::type &
            {
                static_assert(Index < AlternativeCount, "Unknown alternative index to emplace");
                using AlternativeT = typename ThisVariantAlternative<Index>::type;

                return emplace<AlternativeT, Args...>(args...);
            }

            template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1> bool holds_alternative() const
            {
                AWS_ASSERT(m_index != -1);
                return m_index == VariantDetail::Index::GetIndexOf<T, Ts...>();
            }

            template <size_t Index> constexpr bool holds_alternative() const { return Index < AlternativeCount; }

            template <typename T> bool holds_alternative() const { return false; }

            /* non-const get */
            template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1> T &get()
            {
                T *value = reinterpret_cast<T *>(m_storage);
                return *value;
            }

            template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1> T *get_if()
            {
                if (holds_alternative<T>())
                {
                    T *value = reinterpret_cast<T *>(m_storage);
                    return value;
                }
                else
                {
                    return nullptr;
                }
            }

            template <std::size_t Index> auto get() -> typename ThisVariantAlternative<Index>::type &
            {
                static_assert(Index < AlternativeCount, "Unknown alternative index to get");
                AWS_ASSERT(Index == m_index);
                using AlternativeT = typename ThisVariantAlternative<Index>::type;
                AlternativeT *ret = reinterpret_cast<AlternativeT *>(m_storage);
                return *ret;
            }

            /* const get */
            template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1> const T &get() const
            {
                const T *value = reinterpret_cast<const T *>(m_storage);
                return *value;
            }

            template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1> const T *get_if() const
            {
                if (holds_alternative<T>())
                {
                    T *value = reinterpret_cast<T *>(m_storage);
                    return value;
                }
                else
                {
                    return nullptr;
                }
            }

            template <std::size_t Index> auto get() const -> const typename ThisVariantAlternative<Index>::type &
            {
                static_assert(Index < AlternativeCount, "Unknown alternative index to get");
                AWS_ASSERT(Index == m_index);
                using AlternativeT = typename ThisVariantAlternative<Index>::type;
                const AlternativeT *ret = reinterpret_cast<const AlternativeT *>(m_storage);
                return *ret;
            }

            /* This is just a templated way to say
             * "int*" for
             * a VariantAlternative<0, Variant<int, char, long>()>*/
            template <std::size_t Index>
            using RawAlternativePointerT =
                typename std::add_pointer<typename ThisVariantAlternative<Index>::type>::type;

            template <std::size_t Index> auto get_if() -> RawAlternativePointerT<Index>
            {
                static_assert(Index < AlternativeCount, "Unknown alternative index to get");
                if (holds_alternative<Index>())
                {
                    using AlternativePtrT = RawAlternativePointerT<Index>;
                    AlternativePtrT value = reinterpret_cast<AlternativePtrT>(m_storage);
                    return value;
                }
                else
                {
                    return nullptr;
                }
            }

            template <std::size_t Index>
            using ConstRawAlternativePointerT = typename std::add_pointer<
                typename std::add_const<typename ThisVariantAlternative<Index>::type>::type>::type;

            template <std::size_t Index> auto get_if() const -> ConstRawAlternativePointerT<Index>
            {
                static_assert(Index < AlternativeCount, "Unknown alternative index to get");
                if (holds_alternative<Index>())
                {
                    using AlternativePtrT = ConstRawAlternativePointerT<Index>;
                    AlternativePtrT value = reinterpret_cast<AlternativePtrT>(m_storage);
                    return value;
                }
                else
                {
                    return nullptr;
                }
            }

            std::size_t index() const { return m_index; }

            ~Variant() { Destroy(); }

          private:
            static constexpr std::size_t STORAGE_SIZE = VariantDetail::ParameterPackSize::GetMaxSizeOf<Ts...>();

            alignas(VariantDetail::ParameterPackSize::AlignAsPack<Ts...>()) char m_storage[STORAGE_SIZE];
            VariantDetail::Index::VariantIndex m_index = -1;

            void Destroy()
            {
                if (m_index != -1)
                {
                    DestroyUtil<Ts...>(m_index);
                }
                m_index = -1;
            }

            template <typename First, typename Second, typename... Rest>
            void DestroyUtil(const std::size_t requestedToDestroy, std::size_t curIndex = 0)
            {
                if (curIndex == requestedToDestroy)
                {
                    First *value = reinterpret_cast<First *>(m_storage);
                    value->~First();
                }
                else
                {
                    DestroyUtil<Second, Rest...>(requestedToDestroy, ++curIndex);
                }
            }

            template <typename Last> void DestroyUtil(const std::size_t requestedToDestroy, std::size_t curIndex = 0)
            {
                AWS_ASSERT(
                    requestedToDestroy == curIndex); // attempting to destroy unknown alternative type in a Variant
                if (curIndex == requestedToDestroy)
                {
                    Last *value = reinterpret_cast<Last *>(m_storage);
                    value->~Last();
                }
            }

            template <short Index, typename First, typename Second, typename... Rest>
            void CopyConstructUtil(const Variant &other)
            {
                static_assert(Index < AlternativeCount, "Attempting to construct unknown Index Type");

                if (Index == other.m_index)
                {
                    using AlternativeT = typename ThisVariantAlternative<Index>::type;
                    new (m_storage) AlternativeT(other.get<Index>());
                    m_index = Index;
                    AWS_ASSERT(m_index != -1);
                }
                else
                {
                    CopyConstructUtil<Index + 1, Second, Rest...>(other);
                }
            }

            template <short Index, typename Last> void CopyConstructUtil(const Variant &other)
            {
                static_assert(Index < AlternativeCount, "Attempting to construct unknown Index Type");

                if (Index == other.m_index)
                {
                    using AlternativeT = typename ThisVariantAlternative<Index>::type;
                    new (m_storage) AlternativeT(other.get<Index>());
                    m_index = Index;
                    AWS_ASSERT(m_index != -1);
                }
                else
                {
                    AWS_ASSERT(!"Unknown variant alternative type in other!");
                }
            }

            template <short Index, typename First, typename Second, typename... Rest>
            void MoveConstructUtil(Variant &&other)
            {
                static_assert(Index < AlternativeCount, "Attempting to construct unknown Index Type");

                if (Index == other.m_index)
                {
                    using AlternativeT = typename ThisVariantAlternative<Index>::type;
                    new (m_storage) AlternativeT(std::move(other.get<Index>()));
                    m_index = Index;
                    AWS_ASSERT(m_index != -1);
                }
                else
                {
                    MoveConstructUtil<Index + 1, Second, Rest...>(std::move(other));
                }
            }

            template <short Index, typename Last> void MoveConstructUtil(Variant &&other)
            {
                static_assert(Index < AlternativeCount, "Attempting to construct unknown Index Type");

                if (Index == other.m_index)
                {
                    using AlternativeT = typename ThisVariantAlternative<Index>::type;
                    new (m_storage) AlternativeT(std::move(other.get<Index>()));
                    m_index = Index;
                    AWS_ASSERT(m_index != -1);
                }
                else
                {
                    AWS_ASSERT(!"Unknown variant alternative type in other!");
                }
            }

            template <short Index, typename First, typename Second, typename... Rest>
            void CopyAssignUtil(const Variant &other)
            {
                static_assert(Index < AlternativeCount, "Attempting to construct unknown Index Type");

                if (Index == other.m_index)
                {
                    using AlternativeT = typename ThisVariantAlternative<Index>::type;
                    AlternativeT *value = reinterpret_cast<AlternativeT *>(m_storage);
                    *value = other.get<Index>();
                    m_index = Index;
                    AWS_ASSERT(m_index != -1);
                }
                else
                {
                    CopyAssignUtil<Index + 1, Second, Rest...>(other);
                }
            }

            template <short Index, typename Last> void CopyAssignUtil(const Variant &other)
            {
                static_assert(Index < AlternativeCount, "Attempting to construct unknown Index Type");

                if (Index == other.m_index)
                {
                    using AlternativeT = typename ThisVariantAlternative<Index>::type;
                    AlternativeT *value = reinterpret_cast<AlternativeT *>(m_storage);
                    *value = other.get<Index>();
                    m_index = Index;
                    AWS_ASSERT(m_index != -1);
                }
                else
                {
                    AWS_ASSERT(!"Unknown variant alternative type in other!");
                }
            }

            template <short Index, typename First, typename Second, typename... Rest>
            void MoveAssignUtil(Variant &&other)
            {
                static_assert(Index < AlternativeCount, "Attempting to construct unknown Index Type");

                if (Index == other.m_index)
                {
                    using AlternativeT = typename ThisVariantAlternative<Index>::type;
                    AlternativeT *value = reinterpret_cast<AlternativeT *>(m_storage);
                    *value = std::move(other.get<Index>());
                    m_index = Index;
                    AWS_ASSERT(m_index != -1);
                }
                else
                {
                    MoveAssignUtil<Index + 1, Second, Rest...>(std::move(other));
                }
            }

            template <short Index, typename Last> void MoveAssignUtil(Variant &&other)
            {
                static_assert(Index < AlternativeCount, "Attempting to construct unknown Index Type");

                if (Index == other.m_index)
                {
                    using AlternativeT = typename ThisVariantAlternative<Index>::type;
                    AlternativeT *value = reinterpret_cast<AlternativeT *>(m_storage);
                    *value = std::move(other.get<Index>());
                    m_index = Index;
                    AWS_ASSERT(m_index != -1);
                }
                else
                {
                    AWS_ASSERT(!"Unknown variant alternative type in other!");
                }
            }
        };

        /* Helper template to get an actual type from an Index */
        template <std::size_t Index, typename... Ts> class VariantAlternative
        {
          public:
            // uses std::tuple as a helper struct to provide index-based access of a parameter pack
            using type = typename std::tuple_element<Index, std::tuple<Ts...>>::type;

            VariantAlternative(const Variant<Ts...> &) {}

            VariantAlternative(const Variant<Ts...> *) {}
        };

        template <typename T> class VariantSize
        {
            constexpr static const std::size_t Value = T::AlternativeCount;
        };
    } // namespace Crt
} // namespace Aws
