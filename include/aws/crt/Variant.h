#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <aws/common/assert.h>
#include <aws/crt/TypeTraits.h>
#include <aws/crt/Utility.h>

#include <algorithm>
#include <type_traits>
#include <utility>

namespace Aws
{
    namespace Crt
    {
        namespace VariantDetail
        {
            template <typename T> constexpr const T &ConstExprMax(const T &a, const T &b)
            {
                return (a < b) ? b : a;
            }

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

                template <typename T, typename Last> constexpr VariantIndex GetIndexOf(VariantIndex curIndex = 0)
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

                // a case when the template parameter pack is empty (i.e. VariantImpl<>)
                template <typename T> constexpr bool ContainsType()
                {
                    return false;
                }

                template <typename T, typename... Ts> struct HasType
                {
                    static const bool value = ContainsType<T, Ts...>();
                };
            } // namespace Checker
#if defined(AWS_CRT_ENABLE_VARIANT_DEBUG)
            namespace VariantDebug
            {
                template <typename... Ts> class VariantDebugBrowser
                {
                  public:
                    VariantDebugBrowser(char *storage) { InitTuple<0, Ts...>(storage); }
                    std::tuple<typename std::add_pointer<Ts>::type...> as_tuple;

                  private:
                    template <IndexT Index, typename First, typename Second, typename... Rest>
                    void InitTuple(char *storage)
                    {
                        First *value = reinterpret_cast<First *>(storage);
                        std::get<Index>(as_tuple) = value;
                        InitTuple<Index + 1, Second, Rest...>(storage);
                    }

                    template <IndexT Index, typename Last> void InitTuple(char *storage)
                    {
                        Last *value = reinterpret_cast<Last *>(storage);
                        std::get<Index>(as_tuple) = value;
                    }
                };
            } // namespace VariantDebug
#endif /* defined(AWS_CRT_ENABLE_VARIANT_DEBUG) */

            /* Depending on the Variant types, this struct either deletes special move members or defaults them. */
            template <bool> class MovableVariant;

            template <> class MovableVariant<true>
            {
              public:
                MovableVariant() = default;
                MovableVariant(const MovableVariant &) = default;
                MovableVariant &operator=(const MovableVariant &) = default;
                MovableVariant(MovableVariant &&other) = default;
                MovableVariant &operator=(MovableVariant &&other) = default;
            };
            template <> class MovableVariant<false>
            {
              public:
                MovableVariant() = default;
                MovableVariant(const MovableVariant &) = default;
                MovableVariant &operator=(const MovableVariant &) = default;
                MovableVariant(MovableVariant &&) = delete;
                MovableVariant &operator=(MovableVariant &&) = delete;
            };

            /* Depending on the Variant types, this struct either deletes special copy members or defaults them. */
            template <bool> class CopyableVariant;

            template <> class CopyableVariant<true>
            {
              public:
                CopyableVariant() = default;
                CopyableVariant(const CopyableVariant &other) = default;
                CopyableVariant &operator=(const CopyableVariant &other) = default;
                CopyableVariant(CopyableVariant &&) = default;
                CopyableVariant &operator=(CopyableVariant &&) = default;
            };

            template <> class CopyableVariant<false>
            {
              public:
                CopyableVariant() = default;

                CopyableVariant(const CopyableVariant &) = delete;
                CopyableVariant &operator=(const CopyableVariant &) = delete;

                CopyableVariant(CopyableVariant &&) = default;
                CopyableVariant &operator=(CopyableVariant &&) = default;
            };

            template <std::size_t Index, typename... Ts> class VariantAlternative;

            template <typename... Ts> class VariantImpl
            {
              private:
                template <std::size_t Index> using ThisVariantAlternative = VariantAlternative<Index, Ts...>;

                template <typename OtherT>
                using EnableIfOtherIsThisVariantAlternative = typename std::enable_if<
                    VariantDetail::Checker::HasType<typename std::decay<OtherT>::type, Ts...>::value,
                    int>::type;

              public:
                using FirstAlternative = typename ThisVariantAlternative<0>::type;

                static constexpr bool isFirstAlternativeNothrowDefaultConstructible =
                    std::is_nothrow_default_constructible<FirstAlternative>::value;

                static constexpr bool isFirstAlternativeDefaultConstructible =
                    std::is_nothrow_constructible<FirstAlternative>::value;

                using IndexT = VariantDetail::Index::VariantIndex;
                static constexpr std::size_t AlternativeCount = sizeof...(Ts);

                VariantImpl() noexcept(isFirstAlternativeNothrowDefaultConstructible)
                {
                    new (m_storage) FirstAlternative();
                    m_index = 0;
                }

                VariantImpl(const VariantImpl &other)
                {
                    AWS_FATAL_ASSERT(other.m_index != -1);
                    m_index = other.m_index;
                    VisitorUtil<0, Ts...>::VisitBinary(this, other, CopyMoveConstructor());
                }

                VariantImpl(VariantImpl &&other)
                {
                    AWS_FATAL_ASSERT(other.m_index != -1);
                    m_index = other.m_index;
                    VisitorUtil<0, Ts...>::VisitBinary(this, std::move(other), CopyMoveConstructor());
                }

                template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1>
                VariantImpl(const T &val) noexcept(
                    std::is_nothrow_constructible<typename std::decay<T>::type, decltype(val)>::value)
                {
                    static_assert(
                        VariantDetail::Checker::HasType<typename std::decay<T>::type, Ts...>::value,
                        "This variant does not have such alternative T.");
                    static_assert(
                        sizeof(T) <= STORAGE_SIZE,
                        "Attempting to instantiate a Variant with a type bigger than all alternatives.");

                    using PlainT = typename std::decay<T>::type;
                    new (m_storage) PlainT(val);
                    m_index = VariantDetail::Index::GetIndexOf<PlainT, Ts...>();
                    AWS_ASSERT(m_index != -1);
                }

                template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1>
                VariantImpl(T &&val) noexcept(
                    std::is_nothrow_constructible<typename std::decay<T>::type, decltype(val)>::value)
                {
                    static_assert(
                        VariantDetail::Checker::HasType<typename std::decay<T>::type, Ts...>::value,
                        "This variant does not have such alternative T.");
                    static_assert(
                        sizeof(T) <= STORAGE_SIZE,
                        "Attempting to instantiate a Variant with a type bigger than all alternatives.");

                    using PlainT = typename std::decay<T>::type;
                    new (m_storage) PlainT(std::forward<T>(val));
                    m_index = VariantDetail::Index::GetIndexOf<PlainT, Ts...>();
                    AWS_ASSERT(m_index != -1);
                }

                // An overload to initialize with an Alternative T in-place
                template <typename T, typename... Args> explicit VariantImpl(Aws::Crt::InPlaceTypeT<T>, Args &&...args)
                {
                    static_assert(
                        VariantDetail::Checker::HasType<typename std::decay<T>::type, Ts...>::value,
                        "This variant does not have such alternative T.");
                    static_assert(
                        sizeof(T) <= STORAGE_SIZE,
                        "Attempting to instantiate a Variant with a type bigger than all alternatives.");

                    using PlainT = typename std::decay<T>::type;
                    new (m_storage) PlainT(std::forward<Args>(args)...);
                    m_index = VariantDetail::Index::GetIndexOf<PlainT, Ts...>();
                    AWS_ASSERT(m_index != -1);
                }

                VariantImpl &operator=(const VariantImpl &other)
                {
                    if (this != &other)
                    {
                        AWS_FATAL_ASSERT(other.m_index != -1);
                        if (m_index != other.m_index)
                        {
                            Destroy();
                            m_index = other.m_index;
                            VisitorUtil<0, Ts...>::VisitBinary(this, other, CopyMoveConstructor());
                        }
                        else
                        {
                            VisitorUtil<0, Ts...>::VisitBinary(this, other, CopyMoveAssigner());
                        }
                    }
                    return *this;
                }

                VariantImpl &operator=(VariantImpl &&other)
                {
                    if (this != &other)
                    {
                        AWS_FATAL_ASSERT(other.m_index != -1);
                        if (m_index != other.m_index)
                        {
                            Destroy();
                            m_index = other.m_index;
                            VisitorUtil<0, Ts...>::VisitBinary(this, std::move(other), CopyMoveConstructor());
                        }
                        else
                        {
                            VisitorUtil<0, Ts...>::VisitBinary(this, std::move(other), CopyMoveAssigner());
                        }
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
                    m_index = VariantDetail::Index::GetIndexOf<PlainT, Ts...>();
                    AWS_ASSERT(m_index != -1);

                    T *value = reinterpret_cast<T *>(m_storage);
                    return *value;
                }

                template <std::size_t Index, typename... Args>
                auto emplace(Args &&...args) -> typename ThisVariantAlternative<Index>::type &
                {
                    static_assert(Index < AlternativeCount, "Unknown alternative index to emplace");
                    using AlternativeT = typename ThisVariantAlternative<Index>::type;

                    return emplace<AlternativeT, Args...>(std::forward<Args>(args)...);
                }

                template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1> bool holds_alternative() const
                {
                    AWS_ASSERT(m_index != -1);
                    return m_index == VariantDetail::Index::GetIndexOf<T, Ts...>();
                }

                /* non-const get */
                template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1> T &get()
                {
                    AWS_FATAL_ASSERT(holds_alternative<T>());
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
                    AWS_FATAL_ASSERT(holds_alternative<Index>());
                    using AlternativeT = typename ThisVariantAlternative<Index>::type;
                    AlternativeT *ret = reinterpret_cast<AlternativeT *>(m_storage);
                    return *ret;
                }

                /* const get */
                template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1> const T &get() const
                {
                    AWS_FATAL_ASSERT(holds_alternative<T>());
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
                 * a VariantAlternative<0, VariantImpl<int, char, long>()>*/
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

                ~VariantImpl() { Destroy(); }

                template <typename VisitorT> void Visit(VisitorT &&visitor)
                {
                    return VisitorUtil<0, Ts...>::Visit(this, std::forward<VisitorT>(visitor));
                }

              private:
                static constexpr std::size_t STORAGE_SIZE = VariantDetail::ParameterPackSize::GetMaxSizeOf<Ts...>();

                alignas(VariantDetail::ParameterPackSize::AlignAsPack<Ts...>()) char m_storage[STORAGE_SIZE];
                IndexT m_index = -1;
#if defined(AWS_CRT_ENABLE_VARIANT_DEBUG)
                VariantDetail::VariantDebug::VariantDebugBrowser<Ts...> browser = m_storage;
#endif /* defined(AWS_CRT_ENABLE_VARIANT_DEBUG) */

                template <size_t Index> constexpr bool holds_alternative() const { return Index == m_index; }

                struct Destroyer
                {
                    template <typename AlternativeT> void operator()(AlternativeT &&value) const
                    {
                        (void)value;
                        using PlaintT = typename std::remove_reference<AlternativeT>::type;
                        value.~PlaintT();
                    }
                };

                void Destroy()
                {
                    AWS_FATAL_ASSERT(m_index != -1);
                    Visit(Destroyer());

                    m_index = -1;
                }

                struct CopyMoveConstructor
                {
                    template <typename AlternativeT> void operator()(AlternativeT &&value, AlternativeT &&other) const
                    {
                        using PlaintT = typename std::remove_reference<AlternativeT>::type;
                        new (&value) PlaintT(std::move<AlternativeT>(other));
                    }

                    template <typename AlternativeT, typename ConstAlternativeT>
                    void operator()(AlternativeT &&value, ConstAlternativeT &other) const
                    {
                        using PlaintT = typename std::remove_reference<AlternativeT>::type;
                        using PlaintOtherT =
                            typename std::remove_const<typename std::remove_reference<AlternativeT>::type>::type;
                        static_assert(std::is_same<PlaintT, PlaintOtherT>::value, "Incompatible types");

                        new (&value) PlaintT(other);
                    }
                };

                struct CopyMoveAssigner
                {
                    template <typename AlternativeT> void operator()(AlternativeT &&value, AlternativeT &&other) const
                    {
                        value = std::move(other);
                    }

                    template <typename AlternativeT, typename ConstAlternativeT>
                    void operator()(AlternativeT &&value, ConstAlternativeT &other) const
                    {
                        using PlaintT = typename std::remove_reference<AlternativeT>::type;
                        using PlaintOtherT =
                            typename std::remove_const<typename std::remove_reference<AlternativeT>::type>::type;
                        static_assert(std::is_same<PlaintT, PlaintOtherT>::value, "Incompatible types");

                        value = other;
                    }
                };

                template <IndexT Index, typename... Args> struct VisitorUtil;

                template <IndexT Index, typename First, typename Second, typename... Rest>
                struct VisitorUtil<Index, First, Second, Rest...>
                {
                    template <typename VisitorStruct> static void Visit(VariantImpl *pThis, VisitorStruct &&visitor)
                    {
                        static_assert(Index < AlternativeCount, "Attempting to visit unknown Index Type");

                        if (Index == pThis->m_index)
                        {
                            using AlternativeT = typename ThisVariantAlternative<Index>::type;
                            AlternativeT *value = reinterpret_cast<AlternativeT *>(pThis->m_storage);
                            visitor(*value);
                        }
                        else
                        {
                            VisitorUtil<static_cast<IndexT>(Index + 1), Second, Rest...>::Visit(
                                pThis, std::forward<VisitorStruct>(visitor));
                        }
                    }

                    template <typename VisitorStruct>
                    static void VisitBinary(
                        VariantImpl<Ts...> *pThis,
                        VariantImpl<Ts...> &&other,
                        VisitorStruct &&visitor)
                    {
                        static_assert(Index < AlternativeCount, "Attempting to visit unknown Index Type");

                        if (Index == pThis->m_index)
                        {
                            using AlternativeT = typename ThisVariantAlternative<Index>::type;
                            AlternativeT *value = reinterpret_cast<AlternativeT *>(pThis->m_storage);
                            visitor(*value, other.get<AlternativeT>());
                        }
                        else
                        {
                            VisitorUtil<static_cast<IndexT>(Index + 1), Second, Rest...>::VisitBinary(
                                pThis, std::forward<VariantImpl<Ts...>>(other), std::forward<VisitorStruct>(visitor));
                        }
                    }

                    template <typename VisitorStruct>
                    static void VisitBinary(
                        VariantImpl<Ts...> *pThis,
                        const VariantImpl<Ts...> &other,
                        VisitorStruct &&visitor)
                    {
                        static_assert(Index < AlternativeCount, "Attempting to visit unknown Index Type");

                        if (Index == pThis->m_index)
                        {
                            using AlternativeT = typename ThisVariantAlternative<Index>::type;
                            AlternativeT *value = reinterpret_cast<AlternativeT *>(pThis->m_storage);
                            const AlternativeT &otherValue = other.get<AlternativeT>();
                            visitor(*value, otherValue);
                        }
                        else
                        {
                            VisitorUtil<static_cast<IndexT>(Index + 1), Second, Rest...>::VisitBinary(
                                pThis, other, std::forward<VisitorStruct>(visitor));
                        }
                    }
                };

                template <IndexT Index, typename Last> struct VisitorUtil<Index, Last>
                {
                    template <typename VisitorStruct> static void Visit(VariantImpl *pThis, VisitorStruct &&visitor)
                    {
                        static_assert(Index < AlternativeCount, "Attempting to visit unknown Index Type");

                        if (Index == pThis->m_index)
                        {
                            using AlternativeT = typename ThisVariantAlternative<Index>::type;
                            AlternativeT *value = reinterpret_cast<AlternativeT *>(pThis->m_storage);
                            visitor(*value);
                        }
                        else
                        {
                            AWS_FATAL_ASSERT(!"Unknown variant alternative to visit!");
                        }
                    }

                    template <typename VisitorStruct>
                    static void VisitBinary(
                        VariantImpl<Ts...> *pThis,
                        VariantImpl<Ts...> &&other,
                        VisitorStruct &&visitor)
                    {
                        static_assert(Index < AlternativeCount, "Attempting to visit unknown Index Type");

                        if (Index == pThis->m_index)
                        {
                            using AlternativeT = typename ThisVariantAlternative<Index>::type;
                            AlternativeT *value = reinterpret_cast<AlternativeT *>(pThis->m_storage);
                            visitor(*value, other.get<AlternativeT>());
                        }
                        else
                        {
                            AWS_FATAL_ASSERT(!"Unknown variant alternative to visit!");
                        }
                    }

                    template <typename VisitorStruct>
                    static void VisitBinary(
                        VariantImpl<Ts...> *pThis,
                        const VariantImpl<Ts...> &other,
                        VisitorStruct &&visitor)
                    {
                        static_assert(Index < AlternativeCount, "Attempting to visit unknown Index Type");

                        if (Index == pThis->m_index)
                        {
                            using AlternativeT = typename ThisVariantAlternative<Index>::type;
                            AlternativeT *value = reinterpret_cast<AlternativeT *>(pThis->m_storage);
                            const AlternativeT &otherValue = other.get<AlternativeT>();
                            visitor(*value, otherValue);
                        }
                        else
                        {
                            AWS_FATAL_ASSERT(!"Unknown variant alternative to visit!");
                        }
                    }
                };
            };

            /* Helper template to get an actual type from an Index */
            template <std::size_t Index, typename... Ts> class VariantAlternative
            {
              public:
                // uses std::tuple as a helper struct to provide index-based access of a parameter pack
                using type = typename std::tuple_element<Index, std::tuple<Ts...>>::type;

                VariantAlternative(const VariantImpl<Ts...> &) {}

                VariantAlternative(const VariantImpl<Ts...> *) {}
            };

            template <typename T> class VariantSize
            {
                constexpr static const std::size_t Value = T::AlternativeCount;
            };

        } // namespace VariantDetail

        /**
         * Custom implementation of a Variant type. std::variant requires C++17.
         * @tparam Ts Types of the variant value.
         */
        template <typename... Ts>
        class Variant : public VariantDetail::MovableVariant<Conjunction<std::is_move_constructible<Ts>...>::value>,
                        public VariantDetail::CopyableVariant<Conjunction<std::is_copy_constructible<Ts>...>::value>
        {
            /* Copyability and Movability depend only on constructors (copy and move correspondingly) of the
             * underlying types. This means that a class with move constructor but with no move assignment operator
             * might cause compilation issues. If such a type ever needs to be supported, additional base class needs
             * to be introduced: MoveAssignable. For now, it'll be overengineering.
             * The same applies to copy constructor and copy assignment operator. */

          private:
            template <std::size_t Index> using ThisVariantAlternative = VariantDetail::VariantAlternative<Index, Ts...>;

            template <typename OtherT>
            using EnableIfOtherIsThisVariantAlternative = typename std::
                enable_if<VariantDetail::Checker::HasType<typename std::decay<OtherT>::type, Ts...>::value, int>::type;

            static constexpr bool isFirstAlternativeNothrowDefaultConstructible =
                VariantDetail::VariantImpl<Ts...>::isFirstAlternativeNothrowDefaultConstructible;

          public:
            using IndexT = VariantDetail::Index::VariantIndex;
            static constexpr std::size_t AlternativeCount = sizeof...(Ts);

            template <
                typename T = typename VariantDetail::VariantImpl<Ts...>::FirstAlternative,
                typename std::enable_if<std::is_default_constructible<T>::value, bool>::type = true>
            Variant() noexcept(isFirstAlternativeNothrowDefaultConstructible)
            {
            }

            template <
                typename T = typename VariantDetail::VariantImpl<Ts...>::FirstAlternative,
                typename std::enable_if<!std::is_default_constructible<T>::value, bool>::type = true>
            Variant() = delete;

            template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1>
            Variant(const T &val) noexcept(
                std::is_nothrow_constructible<typename std::decay<T>::type, decltype(val)>::value)
                : m_variant(val)
            {
            }

            template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1>
            Variant(T &&val) noexcept(std::is_nothrow_constructible<typename std::decay<T>::type, decltype(val)>::value)
                : m_variant(std::forward<T>(val))
            {
            }

            template <typename T, typename... Args>
            explicit Variant(Aws::Crt::InPlaceTypeT<T> ipt, Args &&...args)
                : m_variant(ipt, std::forward<Args>(args)...)
            {
            }

            template <typename T, typename... Args, EnableIfOtherIsThisVariantAlternative<T> = 1>
            T &emplace(Args &&...args)
            {
                return m_variant.template emplace<T>(std::forward<Args>(args)...);
            }

            template <std::size_t Index, typename... Args>
            auto emplace(Args &&...args) -> typename ThisVariantAlternative<Index>::type &
            {
                return m_variant.template emplace<Index>(std::forward<Args>(args)...);
            }

            template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1> bool holds_alternative() const
            {
                return m_variant.template holds_alternative<T>();
            }

            template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1> T &get()
            {
                return m_variant.template get<T>();
            }

            template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1> T *get_if()
            {
                return m_variant.template get_if<T>();
            }

            template <std::size_t Index> auto get() -> typename ThisVariantAlternative<Index>::type &
            {
                return m_variant.template get<Index>();
            }

            template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1> const T &get() const
            {
                return m_variant.template get<T>();
            }

            template <typename T, EnableIfOtherIsThisVariantAlternative<T> = 1> const T *get_if() const
            {
                return m_variant.template get_if<T>();
            }

            template <std::size_t Index> auto get() const -> const typename ThisVariantAlternative<Index>::type &
            {
                return m_variant.template get<Index>();
            }

            template <std::size_t Index>
            using RawAlternativePointerT =
                typename std::add_pointer<typename ThisVariantAlternative<Index>::type>::type;

            template <std::size_t Index> auto get_if() -> RawAlternativePointerT<Index>
            {
                return m_variant.template get_if<Index>();
            }

            template <std::size_t Index>
            using ConstRawAlternativePointerT = typename std::add_pointer<
                typename std::add_const<typename ThisVariantAlternative<Index>::type>::type>::type;

            template <std::size_t Index> auto get_if() const -> ConstRawAlternativePointerT<Index>
            {
                return m_variant.template get_if<Index>();
            }

            std::size_t index() const { return m_variant.index(); }

            template <typename VisitorT> void Visit(VisitorT &&visitor)
            {
                m_variant.Visit(std::forward<VisitorT>(visitor));
            }

          private:
            VariantDetail::VariantImpl<Ts...> m_variant;
        };

    } // namespace Crt
} // namespace Aws
