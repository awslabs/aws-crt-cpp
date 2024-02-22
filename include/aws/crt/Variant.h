#pragma once
/**
 * Copyright Amazon.com, Inc. or its affiliates. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0.
 */

#include <utility>
#include <algorithm>
#include <cassert>
#include <type_traits>

namespace Aws
{
    namespace Crt
    {
        namespace VariantDetail
        {
            template<typename T>
            constexpr const T& ConstExprMax(const T& a, const T& b)
            {
                return (a < b) ? b : a;
            }

            namespace ParameterPackSize
            {
                // Returns a max of sizeof(T) over all T in a template parameter pack
                template<typename Last>
                constexpr std::size_t GetMaxSizeOf(std::size_t curMax = 0) {
                    return ConstExprMax(curMax, sizeof(Last));
                }


                template<typename First, typename Second, typename ...Rest>
                constexpr std::size_t GetMaxSizeOf(std::size_t curMax = 0) {
                    return ConstExprMax(curMax, GetMaxSizeOf<Second, Rest...>(sizeof(First)));
                }
            }

            namespace Index
            {
                template<typename T, typename Last>
                constexpr std::size_t GetIndexOf(std::size_t curIndex = 0) {
                    return std::is_same<T, Last>::value ? curIndex : -1;
                }

                template<typename T, typename First, typename Second, typename ...Rest>
                constexpr std::size_t GetIndexOf(std::size_t curIndex = 0) {
                    return std::is_same<T, First>::value ? curIndex : GetIndexOf<T, Second, Rest...>(curIndex++);
                }
            }

            namespace Checker
            {
                // Returns True if the template parameter pack Ts has a type T, i.e. ContainsType<T, Ts>() == true if T is in the list of Ts
//                template<typename T, typename First, typename Second, typename ...Rest>
//                constexpr bool ContainsType() {
//                    return std::is_same<T, First>::value || ContainsType<T, Second, Rest...>();
//                }
                template<typename T, typename Last>
                constexpr bool ContainsType() {
                    return std::is_same<T, Last>::value;
                }

                template<typename T, typename First, typename Second, typename ...Rest>
                constexpr bool ContainsType() {
                    return std::is_same<T, First>::value || ContainsType<T, Second, Rest...>();
                }

//                template<typename T, typename ...Ts>
//                constexpr bool ContainsType() {
//                    return ContainsType<T, Ts...>();
//                }



                // a case when the template parameter pack is empty (i.e. Variant<>)
                template<typename T>
                constexpr bool ContainsType() {
                    return false;
                }

                template<typename T, typename ...Ts>
                struct HasType
                {
                    static const bool value = ContainsType<T, Ts...>();
                };
            }
        }

        template <std::size_t Index, typename... Ts> class VariantAlternative;

        /**
         * Custom implementation of a Variant type. std::variant requires C++17
         * @tparam Ts types of the variant value
         */
        template <typename... Ts> class Variant
        {
          public:
            static constexpr std::size_t AlternativeCount = sizeof...(Ts);

            template<std::size_t Index>
            using VariantAlternative = VariantAlternative<Index, Variant<Ts...>>;

            using ThisVariantT = Variant<Ts...>;
            template<typename OtherT>
            using EnableIfOtherIsSameVariantType = typename std::enable_if<std::is_same<typename std::decay<OtherT>, ThisVariantT>::value, int>::type;

            template<typename OtherT>
            using EnableIfOtherIsThisVariantAlternative = typename std::enable_if<VariantDetail::Checker::HasType<typename std::decay<OtherT>::type, Ts...>::value, int>::type;

            Variant() : m_value(nullptr) {}

            // SFINAE to force compiler to choose a correct overload between Variant(anotherVariant) and Variant(alternative) constructors
            template<typename VariantT = ThisVariantT, EnableIfOtherIsSameVariantType<VariantT> = 1>
            Variant(const VariantT &other)
            {
                auto otherIdx = other.m_index;
                if (otherIdx != -1)
                {
                    CopyAssignUtil(other);
                }
            }

            template<typename VariantT = ThisVariantT, EnableIfOtherIsSameVariantType<VariantT> = 1>
            Variant(VariantT &&other)
            {
                auto otherIdx = other.m_index;
                if (otherIdx != -1)
                {
                    MoveAssignUtil(other);
                }
            }

            template<typename T, EnableIfOtherIsThisVariantAlternative<T> = 1>
            Variant(const T &val)
            {
                static_assert(VariantDetail::Checker::ContainsType<T, Ts...>(), "This variant does not have such alternative T.");
                static_assert(sizeof(T) >= STORAGE_SIZE, "Attempting to instantiate a Variant with a type bigger than all alternatives.");

                using PlainT = typename std::decay<T>::type;
                new (m_storage) PlainT(val);
                m_value = reinterpret_cast<T *>(m_storage);
                m_index = VariantDetail::Index::GetIndexOf<T, Ts...>();
                assert(m_index != -1);
            }

            template<typename T, EnableIfOtherIsThisVariantAlternative<T> = 1>
            Variant(T &&val)
            {
                //static_assert(VariantDetail::Checker::ContainsType<std::remove_cv<T>, Ts...>(), "This variant does not have such alternative T.");
                static_assert(sizeof(T) >= STORAGE_SIZE, "Attempting to instantiate a Variant with a type bigger than all alternatives.");

                using PlainT = typename std::decay<T>::type;
                new (m_storage) PlainT(std::forward<T>(val));
                m_value = reinterpret_cast<T *>(m_storage);
                m_index = VariantDetail::Index::GetIndexOf<T, Ts...>();
                assert(m_index != -1);
            }

            template<typename VariantT = ThisVariantT, EnableIfOtherIsSameVariantType<VariantT> = 1>
            Variant &operator=(const VariantT& other)
            {
                if (this == &other)
                {
                    return *this;
                }
                if (this->m_value)
                {
                    DestroyUtil<Ts...>(this->m_index);
                }

                CopyAssignUtil(other);

                return *this;
            }

            template<typename VariantT = ThisVariantT, EnableIfOtherIsSameVariantType<VariantT> = 1>
            Variant &operator=(VariantT &&other)
            {
                if (this == &other)
                {
                    return *this;
                }
                if (this->m_value)
                {
                    DestroyUtil<Ts...>(this->m_index);
                }

                MoveAssignUtil(other);

                return *this;
            }

            template<typename T, EnableIfOtherIsThisVariantAlternative<T> = 1>
            bool holds_alternative() const
            {
                assert(m_value);
                assert(m_index != -1);
                return m_index == VariantDetail::Index::GetIndexOf<T, Ts...>();
            }

            template<typename T, EnableIfOtherIsThisVariantAlternative<T> = 1>
            const T& get() const
            {
                assert(holds_alternative<T>);
                return *reinterpret_cast<T *>(m_value);
            }

            template<typename T, EnableIfOtherIsThisVariantAlternative<T> = 1>
            const T * get_if() const
            {
                if (holds_alternative<T>())
                    return reinterpret_cast<T *>(m_value);
                else
                    return nullptr;
            }

            template<std::size_t Index>
            const VariantAlternative<Index>& get() const
            {
                assert(Index == m_index);
                return VariantAlternative<Index>(*this);
            }

            /* This is just a templated way to say
             * "int*" for
             * a VariantAlternative<0, Variant<int, char, long>()>*/
            template< std::size_t Index >
            using RawAlternativePointerT = typename std::add_pointer<VariantAlternative<Index>>::type;

            template<std::size_t Index>
            const RawAlternativePointerT<Index> * get_if() const
            {
                if (holds_alternative<Index>())
                    return reinterpret_cast<RawAlternativePointerT<Index> *>(m_value);
                else
                    return nullptr;
            }

            std::size_t index() const
            {
                return m_index;
            }

            ~Variant()
            {
                Destroy();
            }

          private:
            static constexpr std::size_t STORAGE_SIZE = VariantDetail::ParameterPackSize::GetMaxSizeOf<Ts...>();

            alignas(Ts...) char m_storage[STORAGE_SIZE];
            void *m_value = nullptr;
            std::size_t m_index = -1;

            void Destroy()
            {
                if (m_value)
                {
                    DestroyUtil<Ts...>(m_index);
                }
                m_value = nullptr;
                m_index = -1;
            }

            template<typename First, typename Second, typename ...Rest>
            void DestroyUtil(const std::size_t requestedToDestroy, std::size_t curIndex = 0)
            {
                if (curIndex == requestedToDestroy)
                {
                    reinterpret_cast<First *>(m_storage)->~First();
                }
                else
                {
                    DestroyUtil<Second, Rest...>(requestedToDestroy, curIndex++);
                }
            }

            template<typename Last>
            void DestroyUtil(const std::size_t requestedToDestroy, std::size_t curIndex = 0)
            {
                assert(requestedToDestroy == curIndex); // attempting to destroy unknown alternative type in a Variant
                if (curIndex == requestedToDestroy)
                {
                    reinterpret_cast<Last *>(m_storage)->~Last();
                }
            }

            template<std::size_t Index = 0>
            void CopyAssignUtil(const Variant& other)
            {
                static_assert(Index <= AlternativeCount, "Attempting to construct unknown Index Type");

                if (Index == other.m_index)
                {
                    new (m_storage) typename VariantAlternative<Index>::type (std::forward<typename VariantAlternative<Index>::type>(other.get<Index>()));
                    m_value = reinterpret_cast<typename VariantAlternative<Index>::type *>(m_storage);
                    m_index = Index;
                    assert(m_index != -1);
                }
                else
                {
                    CopyAssignUtil<Index+1>(other);
                }
            }

            template<std::size_t Index = 0>
            void MoveAssignUtil(Variant&& other)
            {
                static_assert(Index <= AlternativeCount, "Attempting to construct unknown Index Type");

                if (Index == other.m_index)
                {
                    new (m_storage) typename VariantAlternative<Index>::type (std::move<typename VariantAlternative<Index>::type>(other.get<Index>()));
                    m_value = reinterpret_cast<typename VariantAlternative<Index>::type *>(m_storage);
                    m_index = Index;
                    assert(m_index != -1);
                }
                else
                {
                    MoveAssignUtil<Index+1>(other);
                }
            }
        };

        /* Helper template to get an actual type from an Index */
        template <std::size_t Index, typename... Ts>
        class VariantAlternative
        {
        public:
            // uses std::tuple as a helper struct to provide index-based access of a parameter pack
            using type = typename std::tuple_element<Index, std::tuple<Ts...>>::type;

            VariantAlternative(const Variant<Ts...>&)
            {}

            VariantAlternative(const Variant<Ts...>*)
            {}
        };

        template< typename T >
        class VariantSize
        {
            constexpr static const std::size_t Value = T::AlternativeCount;
        };
    } // namespace Crt
} // namespace Aws
