#ifndef DYNABRIDGE_TYPE_LIST
#define DYNABRIDGE_TYPE_LIST

#include <cstddef>
#include <type_traits>
#include <utility>

namespace dynabridge {
    template <size_t I, typename T>
    struct type_list_leaf { using type = T; };

    template <typename Is, typename ... Ts>
    struct type_list_base;

    template <size_t ... idx, typename ... Ts>
    struct type_list_base<std::index_sequence<idx...>, Ts...> 
        : type_list_leaf<idx, Ts>... { };

    template <typename ... Ts>
    struct type_list 
        : type_list_base<std::index_sequence_for<Ts...>, Ts...> { 
        
        template <size_t I, typename T>
        static typename type_list_leaf<I, T>::type element_at(type_list_leaf<I, T>) noexcept;
    };

    template <typename T>
    struct type_list_size;

    template <typename ... Ts>
    struct type_list_size<type_list<Ts...>>
        : std::integral_constant<size_t, sizeof...(Ts)> {
    };

    template <size_t I, typename T>
    struct element_at;

    template <size_t I, typename ... Ts>
    struct element_at <I, type_list<Ts...>> {
        static_assert(I < sizeof...(Ts), "Index is outof range");
        using type = decltype(type_list<Ts...>::template element_at<I>(std::declval<type_list<Ts...>>()));    
    };

    template <size_t I, typename T>
    using element_at_t = typename element_at<I, T>::type;

    // prepend
    template <typename T, typename TL>
    struct prepend;
    
    template <typename T, typename ... Ts>
    struct prepend <T, type_list<Ts...>> {
        using type = type_list<T, Ts...>;
    };

    template <typename T, typename TL>
    using prepend_t = typename prepend<T, TL>::type;
}

#endif
