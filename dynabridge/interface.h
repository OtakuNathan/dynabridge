#ifndef DYNABRIDGE_INTERFACE_H
#define DYNABRIDGE_INTERFACE_H

#include <type_traits>

#include "traits.h"
#include "type_list.h"

#ifndef DYNABRIDGE_METHOD_INDEX
    // Unlike __COUNTER__, __LINE__ is stable when the same declaration table is
    // expanded in translation units with different preprocessing histories.
    #define DYNABRIDGE_METHOD_INDEX __LINE__
#endif

namespace dynabridge {
    template <typename Descriptor>
    struct interface_method_names;

    template <>
    struct interface_method_names<void> {
        using type = type_list<>;
    };

    template <typename Class>
    struct class_interfaces;

    template <typename Class>
    struct class_method_names;

    template <std::size_t Index>
    struct method_index {
    };

    template <typename Receiver, bool = std::is_void<Receiver>::value>
    struct interface_receiver_metadata {
        using receiver_symbol_t = Receiver;
    };

    template <typename Receiver>
    struct interface_receiver_metadata<Receiver, true> {
    };

    namespace interface_detail {
        constexpr bool equal_name(const char* left, const char* right) noexcept {
            return *left == *right
                && (*left == '\0' || equal_name(left + 1, right + 1));
        }

        template <typename... Lists>
        struct concat;

        template <>
        struct concat<> {
            using type = type_list<>;
        };

        template <typename... Ts>
        struct concat<type_list<Ts...>> {
            using type = type_list<Ts...>;
        };

        template <typename... Left, typename... Right, typename... Tail>
        struct concat<type_list<Left...>, type_list<Right...>, Tail...>
            : concat<type_list<Left..., Right...>, Tail...> {
        };

        template <typename Descriptor, std::size_t Index, typename = void>
        struct method_at_index {
            using type = type_list<>;
        };

        // X-macro blocks expose one overload per declaration index. A balanced
        // scan recovers each block's method list without global state.
        template <typename Descriptor, std::size_t Index>
        struct method_at_index<Descriptor, Index, void_t<decltype(
            Descriptor::method_name_at(method_index<Index>{}))>> {
            using type = type_list<decltype(
                Descriptor::method_name_at(method_index<Index>{}))>;
        };

        template <typename Descriptor, std::size_t Begin, std::size_t End,
            bool Empty = (Begin >= End), bool Single = (Begin + 1 >= End)>
        struct names_in_range;

        template <typename Descriptor, std::size_t Begin, std::size_t End, bool Single>
        struct names_in_range<Descriptor, Begin, End, true, Single> {
            using type = type_list<>;
        };

        template <typename Descriptor, std::size_t Begin, std::size_t End>
        struct names_in_range<Descriptor, Begin, End, false, true>
            : method_at_index<Descriptor, Begin> {
        };

        template <typename Descriptor, std::size_t Begin, std::size_t End>
        struct names_in_range<Descriptor, Begin, End, false, false> {
        private:
            static constexpr std::size_t middle = Begin + (End - Begin) / 2;

        public:
            using type = typename concat<
                typename names_in_range<Descriptor, Begin, middle>::type,
                typename names_in_range<Descriptor, middle, End>::type>::type;
        };

        template <typename T, typename List>
        struct contains_type;

        template <typename T>
        struct contains_type<T, type_list<>> : std::false_type {
        };

        template <typename T, typename Head, typename... Tail>
        struct contains_type<T, type_list<Head, Tail...>>
            : std::conditional<
                std::is_same<T, Head>::value,
                std::true_type,
                contains_type<T, type_list<Tail...>>>::type {
        };

        template <typename List>
        struct unique_types;

        template <>
        struct unique_types<type_list<>> : std::true_type {
        };

        template <typename... Tail>
        struct unique_types<type_list<void, Tail...>>
            : unique_types<type_list<Tail...>> {
        };

        template <typename Head, typename... Tail>
        struct unique_types<type_list<Head, Tail...>>
            : std::integral_constant<bool,
                !contains_type<Head, type_list<Tail...>>::value
                && unique_types<type_list<Tail...>>::value> {
        };

        template <typename T, typename List>
        struct contains_name;

        template <typename T>
        struct contains_name<T, type_list<>> : std::false_type {
        };

        template <typename T, typename... Tail>
        struct contains_name<T, type_list<void, Tail...>>
            : contains_name<T, type_list<Tail...>> {
        };

        template <typename T, typename Head, typename... Tail>
        struct contains_name<T, type_list<Head, Tail...>>
            : std::integral_constant<bool,
                equal_name(T::symbol_name(), Head::symbol_name())
                || contains_name<T, type_list<Tail...>>::value> {
        };

        template <typename List>
        struct unique_names;

        template <>
        struct unique_names<type_list<>> : std::true_type {
        };

        template <typename... Tail>
        struct unique_names<type_list<void, Tail...>>
            : unique_names<type_list<Tail...>> {
        };

        template <typename Head, typename... Tail>
        struct unique_names<type_list<Head, Tail...>>
            : std::integral_constant<bool,
                !contains_name<Head, type_list<Tail...>>::value
                && unique_names<type_list<Tail...>>::value> {
        };

        template <typename Interfaces>
        struct collect_interface_methods;

        template <typename... Interfaces>
        struct collect_interface_methods<type_list<Interfaces...>> {
            using type = typename concat<
                typename interface_method_names<Interfaces>::type...>::type;
        };

        template <typename Host, typename Descriptor, bool = std::is_void<Descriptor>::value>
        struct interface_base;

        template <typename Host, typename Descriptor>
        struct interface_base<Host, Descriptor, false>
            : Descriptor::template mixin_t<Host> {
        };

        template <typename Host, typename Descriptor>
        struct interface_base<Host, Descriptor, true> {
        };

        template <typename Host, typename Interfaces>
        struct unchecked_interface_pack;

        template <typename Host, typename... Interfaces>
        struct unchecked_interface_pack<Host, type_list<Interfaces...>>
            : interface_base<Host, Interfaces>... {
        };

        template <typename Receiver, typename Descriptor,
            bool = std::is_void<Descriptor>::value>
        struct interface_symbol_base;

        template <typename Receiver, typename Descriptor>
        struct interface_symbol_base<Receiver, Descriptor, false>
            : Descriptor::template symbols_t<Receiver> {
        };

        template <typename Receiver, typename Descriptor>
        struct interface_symbol_base<Receiver, Descriptor, true> {
        };

        template <typename Receiver, typename Interfaces>
        struct interface_symbol_pack;

        template <typename Receiver, typename... Interfaces>
        struct interface_symbol_pack<Receiver, type_list<Interfaces...>>
            : interface_symbol_base<Receiver, Interfaces>... {
        };

        template <
            typename Host,
            typename Interfaces,
            typename OwnMethods,
            bool UniqueInterfaces = unique_types<Interfaces>::value,
            bool UniqueMethods = unique_names<typename concat<
                typename collect_interface_methods<Interfaces>::type,
                OwnMethods>::type>::value>
        struct checked_interface_pack;

        template <typename Host, typename Interfaces, typename OwnMethods>
        struct checked_interface_pack<Host, Interfaces, OwnMethods, true, true>
            : unchecked_interface_pack<Host, Interfaces> {
        };

        template <typename Host, typename Interfaces, typename OwnMethods, bool UniqueMethods>
        struct checked_interface_pack<Host, Interfaces, OwnMethods, false, UniqueMethods> {
            static_assert(unique_types<Interfaces>::value,
                "A bridge class cannot IMPLEMENTS the same interface more than once.");
        };

        template <typename Host, typename Interfaces, typename OwnMethods>
        struct checked_interface_pack<Host, Interfaces, OwnMethods, true, false> {
            static_assert(unique_names<typename concat<
                    typename collect_interface_methods<Interfaces>::type,
                    OwnMethods>::type>::value,
                "Implemented interfaces and bridge class members must have unique names.");
        };

        template <typename Host, typename Interfaces, typename OwnMethods>
        struct checked_interface_pack<Host, Interfaces, OwnMethods, false, false> {
            static_assert(unique_types<Interfaces>::value,
                "A bridge class cannot IMPLEMENTS the same interface more than once.");
            static_assert(unique_names<typename concat<
                    typename collect_interface_methods<Interfaces>::type,
                    OwnMethods>::type>::value,
                "Implemented interfaces and bridge class members must have unique names.");
        };
    }

    template <typename Host, typename Interfaces, typename OwnMethods>
    using interface_pack = interface_detail::checked_interface_pack<
        Host, Interfaces, OwnMethods>;

    template <typename Receiver, typename Interfaces>
    using interface_symbol_pack = interface_detail::interface_symbol_pack<
        Receiver, Interfaces>;

    template <typename Descriptor, std::size_t Begin, std::size_t End>
    using method_names_in_range_t = typename interface_detail::names_in_range<
        Descriptor, Begin + 1, End>::type;

    template <typename Descriptor>
    struct interface_method_names {
    private:
        using symbols_t = typename Descriptor::method_symbols_t;

    public:
        using type = method_names_in_range_t<
            symbols_t, symbols_t::begin_index, symbols_t::end_index>;
    };

    template <typename Class>
    struct class_method_names {
        using type = method_names_in_range_t<
            Class, Class::begin_index, Class::end_index>;
    };

    template <typename Interfaces>
    struct are_interface_types_unique
        : interface_detail::unique_types<Interfaces> {
    };

    template <typename Interfaces, typename OwnMethods>
    struct are_interface_method_names_unique
        : interface_detail::unique_names<typename interface_detail::concat<
            typename interface_detail::collect_interface_methods<Interfaces>::type,
            OwnMethods>::type> {
    };
}

#endif // DYNABRIDGE_INTERFACE_H
