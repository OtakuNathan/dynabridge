#ifndef DYNABRIDGE_OPTIONAL_H
#define DYNABRIDGE_OPTIONAL_H

#include <memory>
#include <type_traits>
#include <utility>

#include "inplace.h"

namespace dynabridge {
    template <typename T, bool = std::is_trivially_destructible<T>::value>
    union optional_payload;

    template <typename T>
    union optional_payload<T, true> {
        unsigned char empty_;
        T value_;

        optional_payload() noexcept
            : empty_{} {
        }

        template <typename... Args>
        explicit optional_payload(in_place_t, Args&&... args)
            : value_(std::forward<Args>(args)...) {
        }

        ~optional_payload() = default;
    };

    template <typename T>
    union optional_payload<T, false> {
        unsigned char empty_;
        T value_;

        optional_payload() noexcept
            : empty_{} {
        }

        template <typename... Args>
        explicit optional_payload(in_place_t, Args&&... args)
            : value_(std::forward<Args>(args)...) {
        }

        ~optional_payload() noexcept {
        }
    };

    template <typename T, bool = std::is_trivially_destructible<T>::value>
    class optional_storage_base;

    template <typename T>
    class DYNABRIDGE_EMPTY_BASES optional_storage_base<T, true> {
    public:
        using value_type = T;

        optional_storage_base() noexcept = default;

        template <typename... Args>
        explicit optional_storage_base(in_place_t, Args&&... args)
            noexcept(std::is_nothrow_constructible<T, Args&&...>::value)
            : payload_(in_place, std::forward<Args>(args)...),
              has_value_(true) {
        }

        ~optional_storage_base() = default;

        bool has_value() const noexcept {
            return has_value_;
        }

        template <typename... Args>
        T& construct(Args&&... args)
            noexcept(std::is_nothrow_constructible<T, Args&&...>::value)
        {
            ::new (static_cast<void*>(std::addressof(payload_.value_)))
                T(std::forward<Args>(args)...);
            has_value_ = true;
            return get();
        }

        void reset() noexcept {
            has_value_ = false;
        }

        T& get() noexcept {
            return payload_.value_;
        }

        const T& get() const noexcept {
            return payload_.value_;
        }

    private:
        optional_payload<T> payload_;
        bool has_value_ = false;
    };

    template <typename T>
    class DYNABRIDGE_EMPTY_BASES optional_storage_base<T, false> {
    public:
        using value_type = T;

        optional_storage_base() noexcept = default;

        template <typename... Args>
        explicit optional_storage_base(in_place_t, Args&&... args)
            noexcept(std::is_nothrow_constructible<T, Args&&...>::value)
            : payload_(in_place, std::forward<Args>(args)...),
              has_value_(true) {
        }

        ~optional_storage_base() noexcept {
            reset();
        }

        bool has_value() const noexcept {
            return has_value_;
        }

        template <typename... Args>
        T& construct(Args&&... args)
            noexcept(std::is_nothrow_constructible<T, Args&&...>::value)
        {
            ::new (static_cast<void*>(std::addressof(payload_.value_)))
                T(std::forward<Args>(args)...);
            has_value_ = true;
            return get();
        }

        void reset() noexcept {
            if (has_value_) {
                payload_.value_.~T();
                has_value_ = false;
            }
        }

        T& get() noexcept {
            return payload_.value_;
        }

        const T& get() const noexcept {
            return payload_.value_;
        }

    private:
        optional_payload<T> payload_;
        bool has_value_ = false;
    };

    template <
        typename T,
        bool = std::is_copy_constructible<T>::value,
        bool = std::is_trivially_copy_constructible<T>::value
            && std::is_trivially_destructible<T>::value>
    class optional_copy_construct_base;

    template <typename T, bool Trivial>
    class DYNABRIDGE_EMPTY_BASES optional_copy_construct_base<T, true, Trivial>
        : public optional_storage_base<T> {
        using base_t = optional_storage_base<T>;

    public:
        using base_t::base_t;

        optional_copy_construct_base() = default;
        optional_copy_construct_base(const optional_copy_construct_base&) = default;
        optional_copy_construct_base(optional_copy_construct_base&&) = default;
        optional_copy_construct_base& operator=(const optional_copy_construct_base&) = default;
        optional_copy_construct_base& operator=(optional_copy_construct_base&&) = default;
        ~optional_copy_construct_base() = default;
    };

    template <typename T>
    class DYNABRIDGE_EMPTY_BASES optional_copy_construct_base<T, true, false>
        : public optional_storage_base<T> {
        using base_t = optional_storage_base<T>;

    public:
        using base_t::base_t;

        optional_copy_construct_base() = default;

        optional_copy_construct_base(const optional_copy_construct_base& other)
            noexcept(std::is_nothrow_copy_constructible<T>::value)
            : base_t() {
            if (other.has_value()) {
                this->construct(other.get());
            }
        }

        optional_copy_construct_base(optional_copy_construct_base&&) = default;
        optional_copy_construct_base& operator=(const optional_copy_construct_base&) = default;
        optional_copy_construct_base& operator=(optional_copy_construct_base&&) = default;
        ~optional_copy_construct_base() = default;
    };

    template <typename T>
    class DYNABRIDGE_EMPTY_BASES optional_copy_construct_base<T, false, false>
        : public optional_storage_base<T> {
        using base_t = optional_storage_base<T>;

    public:
        using base_t::base_t;

        optional_copy_construct_base() = default;
        optional_copy_construct_base(const optional_copy_construct_base&) = delete;
        optional_copy_construct_base(optional_copy_construct_base&&) = default;
        optional_copy_construct_base& operator=(const optional_copy_construct_base&) = default;
        optional_copy_construct_base& operator=(optional_copy_construct_base&&) = default;
        ~optional_copy_construct_base() = default;
    };

    template <
        typename T,
        bool = std::is_move_constructible<T>::value,
        bool = std::is_trivially_move_constructible<T>::value
            && std::is_trivially_destructible<T>::value>
    class optional_move_construct_base;

    template <typename T, bool Trivial>
    class DYNABRIDGE_EMPTY_BASES optional_move_construct_base<T, true, Trivial>
        : public optional_copy_construct_base<T> {
        using base_t = optional_copy_construct_base<T>;

    public:
        using base_t::base_t;

        optional_move_construct_base() = default;
        optional_move_construct_base(const optional_move_construct_base&) = default;
        optional_move_construct_base(optional_move_construct_base&&) = default;
        optional_move_construct_base& operator=(const optional_move_construct_base&) = default;
        optional_move_construct_base& operator=(optional_move_construct_base&&) = default;
        ~optional_move_construct_base() = default;
    };

    template <typename T>
    class DYNABRIDGE_EMPTY_BASES optional_move_construct_base<T, true, false>
        : public optional_copy_construct_base<T> {
        using base_t = optional_copy_construct_base<T>;

    public:
        using base_t::base_t;

        optional_move_construct_base() = default;
        optional_move_construct_base(const optional_move_construct_base&) = default;

        optional_move_construct_base(optional_move_construct_base&& other)
            noexcept(std::is_nothrow_move_constructible<T>::value)
            : base_t() {
            if (other.has_value()) {
                this->construct(std::move(other.get()));
            }
        }

        optional_move_construct_base& operator=(const optional_move_construct_base&) = default;
        optional_move_construct_base& operator=(optional_move_construct_base&&) = default;
        ~optional_move_construct_base() = default;
    };

    template <typename T>
    class DYNABRIDGE_EMPTY_BASES optional_move_construct_base<T, false, false>
        : public optional_copy_construct_base<T> {
        using base_t = optional_copy_construct_base<T>;

    public:
        using base_t::base_t;

        optional_move_construct_base() = default;
        optional_move_construct_base(const optional_move_construct_base&) = default;
        optional_move_construct_base(optional_move_construct_base&&) = delete;
        optional_move_construct_base& operator=(const optional_move_construct_base&) = default;
        optional_move_construct_base& operator=(optional_move_construct_base&&) = default;
        ~optional_move_construct_base() = default;
    };

    template <
        typename T,
        bool = std::is_copy_constructible<T>::value && std::is_copy_assignable<T>::value,
        bool = std::is_trivially_copy_constructible<T>::value
            && std::is_trivially_copy_assignable<T>::value
            && std::is_trivially_destructible<T>::value>
    class optional_copy_assign_base;

    template <typename T, bool Trivial>
    class DYNABRIDGE_EMPTY_BASES optional_copy_assign_base<T, true, Trivial>
        : public optional_move_construct_base<T> {
        using base_t = optional_move_construct_base<T>;

    public:
        using base_t::base_t;

        optional_copy_assign_base() = default;
        optional_copy_assign_base(const optional_copy_assign_base&) = default;
        optional_copy_assign_base(optional_copy_assign_base&&) = default;
        optional_copy_assign_base& operator=(const optional_copy_assign_base&) = default;
        optional_copy_assign_base& operator=(optional_copy_assign_base&&) = default;
        ~optional_copy_assign_base() = default;
    };

    template <typename T>
    class DYNABRIDGE_EMPTY_BASES optional_copy_assign_base<T, true, false>
        : public optional_move_construct_base<T> {
        using base_t = optional_move_construct_base<T>;

    public:
        using base_t::base_t;

        optional_copy_assign_base() = default;
        optional_copy_assign_base(const optional_copy_assign_base&) = default;
        optional_copy_assign_base(optional_copy_assign_base&&) = default;

        optional_copy_assign_base& operator=(const optional_copy_assign_base& other)
            noexcept(std::is_nothrow_copy_constructible<T>::value
                && std::is_nothrow_copy_assignable<T>::value)
        {
            if (this != &other) {
                if (this->has_value() && other.has_value()) {
                    this->get() = other.get();
                } else if (other.has_value()) {
                    this->construct(other.get());
                } else {
                    this->reset();
                }
            }
            return *this;
        }

        optional_copy_assign_base& operator=(optional_copy_assign_base&&) = default;
        ~optional_copy_assign_base() = default;
    };

    template <typename T>
    class DYNABRIDGE_EMPTY_BASES optional_copy_assign_base<T, false, false>
        : public optional_move_construct_base<T> {
        using base_t = optional_move_construct_base<T>;

    public:
        using base_t::base_t;

        optional_copy_assign_base() = default;
        optional_copy_assign_base(const optional_copy_assign_base&) = default;
        optional_copy_assign_base(optional_copy_assign_base&&) = default;
        optional_copy_assign_base& operator=(const optional_copy_assign_base&) = delete;
        optional_copy_assign_base& operator=(optional_copy_assign_base&&) = default;
        ~optional_copy_assign_base() = default;
    };

    template <
        typename T,
        bool = std::is_move_constructible<T>::value && std::is_move_assignable<T>::value,
        bool = std::is_trivially_move_constructible<T>::value
            && std::is_trivially_move_assignable<T>::value
            && std::is_trivially_destructible<T>::value>
    class optional_move_assign_base;

    template <typename T, bool Trivial>
    class DYNABRIDGE_EMPTY_BASES optional_move_assign_base<T, true, Trivial>
        : public optional_copy_assign_base<T> {
        using base_t = optional_copy_assign_base<T>;

    public:
        using base_t::base_t;

        optional_move_assign_base() = default;
        optional_move_assign_base(const optional_move_assign_base&) = default;
        optional_move_assign_base(optional_move_assign_base&&) = default;
        optional_move_assign_base& operator=(const optional_move_assign_base&) = default;
        optional_move_assign_base& operator=(optional_move_assign_base&&) = default;
        ~optional_move_assign_base() = default;
    };

    template <typename T>
    class DYNABRIDGE_EMPTY_BASES optional_move_assign_base<T, true, false>
        : public optional_copy_assign_base<T> {
        using base_t = optional_copy_assign_base<T>;

    public:
        using base_t::base_t;

        optional_move_assign_base() = default;
        optional_move_assign_base(const optional_move_assign_base&) = default;
        optional_move_assign_base(optional_move_assign_base&&) = default;
        optional_move_assign_base& operator=(const optional_move_assign_base&) = default;

        optional_move_assign_base& operator=(optional_move_assign_base&& other)
            noexcept(std::is_nothrow_move_constructible<T>::value
                && std::is_nothrow_move_assignable<T>::value)
        {
            if (this != &other) {
                if (this->has_value() && other.has_value()) {
                    this->get() = std::move(other.get());
                } else if (other.has_value()) {
                    this->construct(std::move(other.get()));
                } else {
                    this->reset();
                }
            }
            return *this;
        }

        ~optional_move_assign_base() = default;
    };

    template <typename T>
    class DYNABRIDGE_EMPTY_BASES optional_move_assign_base<T, false, false>
        : public optional_copy_assign_base<T> {
        using base_t = optional_copy_assign_base<T>;

    public:
        using base_t::base_t;

        optional_move_assign_base() = default;
        optional_move_assign_base(const optional_move_assign_base&) = default;
        optional_move_assign_base(optional_move_assign_base&&) = default;
        optional_move_assign_base& operator=(const optional_move_assign_base&) = default;
        optional_move_assign_base& operator=(optional_move_assign_base&&) = delete;
        ~optional_move_assign_base() = default;
    };

    template <typename T>
    class DYNABRIDGE_EMPTY_BASES optional
        : private optional_move_assign_base<T> {
        static_assert(!std::is_void<T>::value, "optional<T> does not support void.");
        static_assert(!std::is_reference<T>::value, "optional<T> stores values, not references.");

        using base_t = optional_move_assign_base<T>;

    public:
        using value_type = T;

        optional() noexcept = default;

        optional(const optional&) = default;
        optional(optional&&) = default;
        optional& operator=(const optional&) = default;
        optional& operator=(optional&&) = default;
        ~optional() = default;

        template <typename U = T,
            typename = typename std::enable_if<std::is_default_constructible<U>::value>::type>
        explicit optional(in_place_t tag)
            noexcept(std::is_nothrow_default_constructible<U>::value)
            : base_t(tag) {
        }

        template <typename... Args>
        explicit optional(in_place_t tag, Args&&... args)
            noexcept(std::is_nothrow_constructible<T, Args&&...>::value)
            : base_t(tag, std::forward<Args>(args)...) {
        }

        template <typename U = T,
            typename = typename std::enable_if<std::is_copy_constructible<U>::value>::type>
        optional(const T& value)
            noexcept(std::is_nothrow_copy_constructible<T>::value)
            : base_t(in_place, value) {
        }

        template <typename U = T,
            typename = typename std::enable_if<std::is_move_constructible<U>::value>::type>
        optional(T&& value)
            noexcept(std::is_nothrow_move_constructible<T>::value)
            : base_t(in_place, std::move(value)) {
        }

        bool has_value() const noexcept {
            return base_t::has_value();
        }

        explicit operator bool() const noexcept {
            return has_value();
        }

        T& operator*() noexcept {
            return base_t::get();
        }

        const T& operator*() const noexcept {
            return base_t::get();
        }

        T* operator->() noexcept {
            return std::addressof(base_t::get());
        }

        const T* operator->() const noexcept {
            return std::addressof(base_t::get());
        }

        T& value() noexcept {
            return base_t::get();
        }

        const T& value() const noexcept {
            return base_t::get();
        }

        template <typename... Args>
        T& emplace(Args&&... args)
            noexcept(std::is_nothrow_constructible<T, Args&&...>::value)
        {
            reset();
            return base_t::construct(std::forward<Args>(args)...);
        }

        void reset() noexcept {
            base_t::reset();
        }
    };
}

#endif //DYNABRIDGE_OPTIONAL_H
