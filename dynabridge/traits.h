//
// Created by wufen on 6/9/2026.
//

#ifndef DYNABRIDGE_TRAITS_H
#define DYNABRIDGE_TRAITS_H

#include <cstddef>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>
#include "optional.h"
#include "type_list.h"

#define DYNABRIDGE_CPP_14 201402L
#define DYNABRIDGE_CPP_17 201703L
#define DYNABRIDGE_CPP_20 202002L

#if defined(_MSVC_LANG)
    #define DYNABRIDGE_CPP_VER _MSVC_LANG
#elif defined(__cplusplus)
    #define DYNABRIDGE_CPP_VER __cplusplus
#else
    #define DYNABRIDGE_CPP_VER 0L
#endif

#define DYNABRIDGE_CPP_AT_LEAST(ver) (DYNABRIDGE_CPP_VER >= DYNABRIDGE_CPP_##ver)
#define DYNABRIDGE_CPP_AT_MOST(ver)  (DYNABRIDGE_CPP_VER <= DYNABRIDGE_CPP_##ver)

namespace dynabridge {

    template <typename T>
    struct type_identity { using type = T; };

    struct bad_conversion : std::runtime_error {
        explicit bad_conversion(const char* message)
            : std::runtime_error(message) {
        }
    };

#if DYNABRIDGE_CPP_AT_MOST(14)
    template <typename T>
    struct negation : std::integral_constant<bool, !T::value> {};

    template <typename ...>
    struct conjunction : std::true_type {};

    template <typename head>
    struct conjunction <head> : head {};

    template <typename head, typename ... tail>
    struct conjunction<head, tail...> :
        std::conditional_t<bool(head::value), conjunction<tail...>, head> {
    };

    template <typename ... Ts>
    constexpr bool conjunction_v = conjunction<Ts...>::value;

    template <typename ...>
    struct disjunction : std::false_type {};

    template <typename head>
    struct disjunction <head> : head {};

    template <typename head, typename ... tail>
    struct disjunction<head, tail...> :
        std::conditional_t<static_cast<bool>(head::value), head, disjunction<tail...>> {
    };

    template <typename ... Ts>
    constexpr bool disjunction_v = disjunction<Ts...>::value;

    namespace swap_adl_tests {
        using std::swap;

        template <typename, typename>
        auto can_swap(...) noexcept(false) -> std::false_type;

        template <typename T, typename U>
        auto can_swap(int) noexcept(noexcept(swap(std::declval<T>(), std::declval<U>()))
            && noexcept(swap(std::declval<U>(), std::declval<T>())))
            -> decltype(
                swap(std::declval<T>(), std::declval<U>()),
                swap(std::declval<U>(), std::declval<T>()),
                std::true_type{}
            );

        template <typename T, typename U, bool = decltype(can_swap<T, U>(0))::value>
        struct is_nothrow_swappable_helper : std::false_type {
        };

        template <typename T, typename U>
        struct is_nothrow_swappable_helper<T, U, true>
            : std::integral_constant<bool,
                noexcept(swap(std::declval<T>(), std::declval<U>()))
                && noexcept(swap(std::declval<U>(), std::declval<T>()))> {
        };
    }

    template<class T, class U = T>
    struct is_swappable : decltype(swap_adl_tests::can_swap<T&, U&>(0)) {
    };

    template <typename R, typename... Args>
    struct is_swappable<R (*)(Args...)> : std::true_type { };

    template<class T, std::size_t N>
    struct is_swappable<T[N], T[N]> : decltype(swap_adl_tests::can_swap<T(&)[N], T(&)[N]>(0)) {
    };

    template<class T, class U = T>
    struct is_nothrow_swappable : swap_adl_tests::is_nothrow_swappable_helper<T&, U&> {
    };

    template <typename R, typename... Args>
    struct is_nothrow_swappable<R (*)(Args...)> : std::true_type { };

    template <typename... Us>
    struct is_swappable <type_list<Us...>> : conjunction<is_swappable<Us>...> { };

    template <typename... Us>
    struct is_nothrow_swappable <type_list<Us...>> : conjunction<is_nothrow_swappable<Us>...> { };

    template <typename ... >
    struct void_ { using type = void; };

    template <typename ...  Ts>
    using void_t = typename void_<Ts...>::type;

    template <typename T>
    constexpr bool is_void_v = std::is_void<T>::value;

    template <typename T>
    constexpr bool is_nothrow_move_constructible_v =
        std::is_nothrow_move_constructible<T>::value;
#else
    using std::void_t;
    using std::conjunction;
    using std::conjunction_v;
    using std::disjunction;
    using std::disjunction_v;
    using std::negation;
    using std::is_swappable;
    using std::is_nothrow_swappable;
    using std::is_void_v;
    using std::is_nothrow_move_constructible_v;
#endif

    struct no_receiver_t { };

    struct import_t { };

    struct export_t { };

    template <typename T, typename = void>
    struct bridge_direction {
        using type = import_t;
    };

    template <typename T>
    struct bridge_direction<T, void_t<typename T::bridge_direction>> {
        using type = typename T::bridge_direction;
    };

    struct unmatched_callable_t { };

    struct construct_object_t { };

    constexpr construct_object_t construct_object { };
    constexpr std::size_t dynamic_arity = static_cast<std::size_t>(-1);

    template <typename T>
    struct borrowed_ref {
        T* value = nullptr;
    };

    template <typename T>
    borrowed_ref<T> borrow(T& value) noexcept {
        return borrowed_ref<T>{std::addressof(value)};
    }

    template <typename T>
    struct owned_value {
        template <typename U>
        explicit owned_value(U&& value)
            : value(std::forward<U>(value)) {
        }

        T value;
    };

    template <typename T>
    owned_value<typename std::decay<T>::type> own(T&& value) {
        return owned_value<typename std::decay<T>::type>(std::forward<T>(value));
    }

    template <typename Native>
    class export_native_storage {
    public:
        using native_t = Native;

        export_native_storage() = delete;

        explicit export_native_storage(borrowed_ref<Native> borrowed) noexcept
            : native_(borrowed.value),
              owns_(false) {
        }

        explicit export_native_storage(owned_value<Native>&& owned)
            noexcept(std::is_nothrow_move_constructible<Native>::value) {
            construct(std::move(owned.value));
        }

        template <
            typename... Args,
            typename = std::enable_if_t<std::is_constructible<Native, Args&&...>::value>>
        explicit export_native_storage(Args&&... args) {
            construct(std::forward<Args>(args)...);
        }

        export_native_storage(const export_native_storage&) = delete;
        export_native_storage& operator=(const export_native_storage&) = delete;

        export_native_storage(export_native_storage&& other)
            noexcept(std::is_nothrow_move_constructible<Native>::value) {
            move_from(std::move(other));
        }

        export_native_storage& operator=(export_native_storage&& other)
            noexcept(std::is_nothrow_move_constructible<Native>::value) {
            if (this != &other) {
                reset();
                move_from(std::move(other));
            }
            return *this;
        }

        ~export_native_storage() noexcept {
            reset();
        }

        Native* get() noexcept {
            return native_;
        }

        const Native* get() const noexcept {
            return native_;
        }

        Native& ref() noexcept {
            return *native_;
        }

        const Native& ref() const noexcept {
            return *native_;
        }

    private:
        using storage_t = typename std::aligned_storage<sizeof(Native), alignof(Native)>::type;

        template <typename... Args>
        void construct(Args&&... args) {
            native_ = new (&storage_) Native(std::forward<Args>(args)...);
            owns_ = true;
        }

        void move_from(export_native_storage&& other) {
            if (other.owns_) {
                construct(std::move(*other.native_));
                other.reset();
            } else {
                native_ = other.native_;
                owns_ = false;
                other.native_ = nullptr;
            }
        }

        void reset() noexcept {
            if (owns_ && native_ != nullptr) {
                native_->~Native();
            }
            native_ = nullptr;
            owns_ = false;
        }

        storage_t storage_;
        Native* native_ = nullptr;
        bool owns_ = false;
    };

    template <typename T, typename Context, typename Value>
    auto to_cast(Context& ctx, Value&& value)
        -> decltype(Context::backend_t::template converter<typename std::decay<T>::type>::to(
            ctx, std::forward<Value>(value)))
    {
        return Context::backend_t::template converter<typename std::decay<T>::type>::to(
            ctx, std::forward<Value>(value));
    }

    template <typename T, typename Context, typename Dynamic>
    auto from_optional(Context& ctx, Dynamic&& value)
        -> decltype(Context::backend_t::template converter<typename std::decay<T>::type>::from(
            ctx, std::forward<Dynamic>(value)))
    {
        return Context::backend_t::template converter<typename std::decay<T>::type>::from(
            ctx, std::forward<Dynamic>(value));
    }

    template <typename T, typename Context, typename Dynamic>
    auto from_cast(Context& ctx, Dynamic&& value)
        -> typename std::decay<decltype(from_optional<T>(
            std::declval<Context&>(),
            std::declval<Dynamic>()))>::type::value_type
    {
        auto result = from_optional<T>(ctx, std::forward<Dynamic>(value));
        if (!result) {
            throw bad_conversion("dynabridge value is not convertible to the requested C++ type");
        }
        return std::move(*result);
    }

    template <typename T>
    struct is_bad_bridge_param
        : std::integral_constant<bool,
            std::is_lvalue_reference<T>::value
            && !std::is_const<typename std::remove_reference<T>::type>::value> {
    };

    template <typename... Args>
    struct are_bridge_params_valid
        : negation<disjunction<is_bad_bridge_param<Args>...>> {
    };

    template <typename T>
    struct is_function_pointer : std::false_type {
    };

    template <typename R, typename... Args>
    struct is_function_pointer<R (*)(Args...)> : std::true_type {
    };

    template <typename T, typename = void>
    struct is_export_overload_binder : std::false_type {
    };

    template <typename T>
    struct is_export_overload_binder<T, void_t<typename T::overloads_t>> : std::true_type {
    };
    }

#endif //DYNABRIDGE_TRAITS_H
