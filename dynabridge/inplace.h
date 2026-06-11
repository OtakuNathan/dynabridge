#ifndef DYNABRIDGE_INPLACE_H
#define DYNABRIDGE_INPLACE_H

#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

#if defined(_MSC_VER) && !defined(__clang__)
#define DYNABRIDGE_EMPTY_BASES __declspec(empty_bases)
#else
#define DYNABRIDGE_EMPTY_BASES
#endif

namespace dynabridge {
    struct in_place_t {
        explicit constexpr in_place_t() noexcept = default;
    };

    constexpr in_place_t in_place{};

    template <typename T>
    struct raw_inplace_storage_operations {
        static_assert(!std::is_void<T>::value, "T must not be void.");

        static T* ptr(void* data) noexcept {
            return reinterpret_cast<T*>(data);
        }

        static const T* ptr(const void* data) noexcept {
            return reinterpret_cast<const T*>(data);
        }

        static void destroy_at(void* data)
            noexcept(std::is_nothrow_destructible<T>::value)
        {
            ptr(data)->~T();
        }

        template <typename... Args>
        static T* construct_at(void* data, Args&&... args)
            noexcept(std::is_nothrow_constructible<T, Args&&...>::value)
        {
            static_assert(
                std::is_constructible<T, Args&&...>::value,
                "T must be constructible with Args.");
            return ::new (data) T(std::forward<Args>(args)...);
        }
    };

    template <typename T, std::size_t Size = sizeof(T), std::size_t Align = alignof(T)>
    struct DYNABRIDGE_EMPTY_BASES raw_inplace_storage
        : private raw_inplace_storage_operations<T> {
        static_assert(sizeof(T) <= Size, "storage size is too small for T.");
        static_assert((Align & (Align - 1)) == 0, "storage alignment must be a power of two.");
        static_assert(Align >= alignof(T), "storage alignment is too small for T.");
        static_assert(Align % alignof(T) == 0, "storage alignment is incompatible with T.");

        using value_type = T;
        using operations_t = raw_inplace_storage_operations<T>;

        raw_inplace_storage() = default;
        ~raw_inplace_storage() = default;

        T* ptr() noexcept {
            return operations_t::ptr(data_);
        }

        const T* ptr() const noexcept {
            return operations_t::ptr(data_);
        }

        template <typename... Args>
        T* construct(Args&&... args)
            noexcept(noexcept(operations_t::construct_at(data_, std::forward<Args>(args)...)))
        {
            return operations_t::construct_at(data_, std::forward<Args>(args)...);
        }

        void destroy()
            noexcept(noexcept(operations_t::destroy_at(data_)))
        {
            operations_t::destroy_at(data_);
        }

    private:
        alignas(Align) unsigned char data_[Size];
    };
}

#endif //DYNABRIDGE_INPLACE_H
