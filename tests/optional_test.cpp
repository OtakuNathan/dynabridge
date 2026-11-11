#include <type_traits>

#include "portal/optional.h"

namespace {
    struct tracked_value {
        static int alive;

        int value = 0;

        explicit tracked_value(int n)
            : value(n) {
            ++alive;
        }

        tracked_value(const tracked_value& other)
            : value(other.value) {
            ++alive;
        }

        tracked_value(tracked_value&& other) noexcept
            : value(other.value) {
            other.value = -1;
            ++alive;
        }

        ~tracked_value() noexcept {
            --alive;
        }
    };

    int tracked_value::alive = 0;
}

int main() {
    static_assert(
        std::is_trivially_destructible<portal::optional<int>>::value,
        "optional<int> should preserve trivial destruction");
    static_assert(
        std::is_trivially_copy_constructible<portal::optional<int>>::value,
        "optional<int> should preserve trivial copy construction");
    static_assert(
        std::is_trivially_move_constructible<portal::optional<int>>::value,
        "optional<int> should preserve trivial move construction");
    static_assert(
        std::is_trivially_copy_assignable<portal::optional<int>>::value,
        "optional<int> should preserve trivial copy assignment");
    static_assert(
        std::is_trivially_move_assignable<portal::optional<int>>::value,
        "optional<int> should preserve trivial move assignment");

    portal::optional<int> empty;
    if (empty) {
        return 1;
    }

    portal::optional<int> value(42);
    if (!value || *value != 42 || value.value() != 42) {
        return 2;
    }

    value.emplace(7);
    if (!value || *value != 7) {
        return 3;
    }

    value.reset();
    if (value) {
        return 4;
    }

    {
        portal::optional<tracked_value> tracked(portal::in_place, 11);
        if (!tracked || tracked->value != 11 || tracked_value::alive != 1) {
            return 5;
        }

        portal::optional<tracked_value> moved(std::move(tracked));
        if (!tracked || !moved || moved->value != 11 || tracked_value::alive != 2) {
            return 6;
        }

        portal::optional<tracked_value> copied(moved);
        if (!copied || copied->value != 11 || tracked_value::alive != 3) {
            return 7;
        }

        moved.reset();
        if (moved || tracked_value::alive != 2) {
            return 8;
        }
    }

    if (tracked_value::alive != 0) {
        return 9;
    }

    return 0;
}
