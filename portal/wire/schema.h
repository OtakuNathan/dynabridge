#ifndef PORTAL_WIRE_SCHEMA_H
#define PORTAL_WIRE_SCHEMA_H

// C++14 value-only wire projection, protocol version 1. This is not C++ object memory
// serialization. All integers are fixed width, little endian; blobs are bytes.
#include <array>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace portal { namespace wire {
static_assert(CHAR_BIT == 8, "typed wire requires eight-bit bytes");
using bytes = std::vector<std::uint8_t>;
enum class errc : std::uint16_t {
    ok = 0, malformed = 1, limit_exceeded = 2, invalid_value = 3,
    unknown_method = 4, incompatible_schema = 5, handler_failed = 6,
    transport_failed = 7
};
struct limits {
    std::size_t max_frame_bytes = 1024 * 1024;
    std::size_t max_decoded_bytes = 4 * 1024 * 1024;
    std::size_t max_elements = 65536;
    std::size_t max_depth = 32;
};
struct view {
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
    view() noexcept = default;
    view(const std::uint8_t* p, std::size_t n) noexcept : data(p), size(n) {}
    explicit view(const bytes& b) noexcept : data(b.data()), size(b.size()) {}
};
constexpr std::uint64_t hash_seed = 14695981039346656037ull;
constexpr std::uint64_t mix(std::uint64_t h, std::uint64_t v) noexcept {
    for (unsigned n = 0; n != 8; ++n) {
        h = (h ^ (v & 255u)) * 1099511628211ull;
        v >>= 8;
    }
    return h;
}
constexpr std::size_t min_sum(std::size_t a, std::size_t b) noexcept {
    return a > (std::numeric_limits<std::size_t>::max)() - b
        ? (std::numeric_limits<std::size_t>::max)() : a + b;
}
constexpr std::size_t min_product(std::size_t a, std::size_t b) noexcept {
    return a && b > (std::numeric_limits<std::size_t>::max)() / a
        ? (std::numeric_limits<std::size_t>::max)() : a * b;
}

class measure {
    std::size_t size_ = 0;
    std::size_t maximum_;
    errc error_ = errc::ok;
public:
    explicit measure(std::size_t maximum) noexcept : maximum_(maximum) {}
    bool fail(errc e) noexcept { if (error_ == errc::ok) error_ = e; return false; }
    bool add(std::size_t n) noexcept {
        if (error_ != errc::ok) return false;
        if (n > maximum_ - size_) return fail(errc::limit_exceeded);
        size_ += n; return true;
    }
    std::size_t size() const noexcept { return size_; }
    errc error() const noexcept { return error_; }
};
class writer {
    std::uint8_t* current_;
    std::size_t remaining_;
public:
    writer(std::uint8_t* p, std::size_t n) noexcept : current_(p), remaining_(n) {}
    template<class U> bool integer(U x) noexcept {
        static_assert(std::is_unsigned<U>::value, "unsigned wire bits required");
        if (sizeof(U) > remaining_) return false;
        for (std::size_t i = 0; i != sizeof(U); ++i)
            current_[i] = static_cast<std::uint8_t>(x >> (i * 8));
        current_ += sizeof(U); remaining_ -= sizeof(U); return true;
    }
    bool raw(const void* p, std::size_t n) noexcept {
        if (n > remaining_ || (n && !p)) return false;
        if (n) { std::memcpy(current_, p, n); current_ += n; remaining_ -= n; }
        return true;
    }
    bool empty() const noexcept { return remaining_ == 0; }
};
class reader {
    const std::uint8_t* current_;
    std::size_t remaining_;
    limits limits_;
    std::size_t charged_ = 0, elements_ = 0, depth_ = 0;
    errc error_ = errc::ok;
public:
    explicit reader(view v, limits l = {}) noexcept
        : current_(v.data), remaining_(v.size), limits_(l) {
        if (v.size && !v.data) fail(errc::malformed);
        if (v.size > l.max_frame_bytes) fail(errc::limit_exceeded);
    }
    bool fail(errc e) noexcept { if (error_ == errc::ok) error_ = e; return false; }
    bool require(std::size_t n) noexcept {
        if (error_ != errc::ok) return false;
        return n <= remaining_ || fail(errc::malformed);
    }
    template<class U> bool integer(U& out) noexcept {
        static_assert(std::is_unsigned<U>::value, "unsigned wire bits required");
        if (!require(sizeof(U))) return false;
        U x = 0;
        for (std::size_t i = 0; i != sizeof(U); ++i)
            x = static_cast<U>(x | (static_cast<U>(current_[i]) << (i * 8)));
        current_ += sizeof(U); remaining_ -= sizeof(U); out = x; return true;
    }
    bool raw(void* p, std::size_t n) noexcept {
        if (!require(n)) return false;
        if (n) { std::memcpy(p, current_, n); current_ += n; remaining_ -= n; }
        return true;
    }
    bool string(std::string& out, std::size_t n) {
        if (!require(n)) return false;
        if (n) {
            out.assign(reinterpret_cast<const char*>(current_), n);
            current_ += n; remaining_ -= n;
        } else out.clear();
        return true;
    }
    bool charge(std::size_t n) noexcept {
        if (n > limits_.max_decoded_bytes - charged_) return fail(errc::limit_exceeded);
        charged_ += n; return true;
    }
    bool elements(std::size_t n) noexcept {
        if (n > limits_.max_elements - elements_) return fail(errc::limit_exceeded);
        elements_ += n; return true;
    }
    bool enter() noexcept {
        if (depth_ == limits_.max_depth) return fail(errc::limit_exceeded);
        ++depth_; return true;
    }
    void leave() noexcept { assert(depth_); --depth_; }
    std::size_t remaining() const noexcept { return remaining_; }
    bool empty() const noexcept { return error_ == errc::ok && remaining_ == 0; }
    errc error() const noexcept { return error_; }
};
struct depth_scope {
    reader& r; bool entered;
    explicit depth_scope(reader& in) noexcept : r(in), entered(in.enter()) {}
    ~depth_scope() { if (entered) r.leave(); }
    depth_scope(const depth_scope&) = delete;
    depth_scope& operator=(const depth_scope&) = delete;
};

namespace detail {
// Template storage gives addressable constants one definition in a header-only
// C++14 library, including when a forwarding API binds them by reference.
template<std::size_t N> struct minimum_size {
    static constexpr std::size_t min_size = N;
};
template<std::size_t N> constexpr std::size_t minimum_size<N>::min_size;
template<std::uint32_t Id> struct field_identity {
    static constexpr std::uint32_t id = Id;
};
template<std::uint32_t Id> constexpr std::uint32_t field_identity<Id>::id;

template<class S, class U> S signed_from_bits(U bits) noexcept {
    const U high = static_cast<U>((std::numeric_limits<S>::max)()) + U(1);
    return bits < high ? static_cast<S>(bits)
        : static_cast<S>((std::numeric_limits<S>::min)() + static_cast<S>(bits - high));
}
template<class T, bool Signed = std::is_signed<T>::value> struct integer_reader;
template<class T> struct integer_reader<T, false> {
    static T value(T x) noexcept { return x; }
};
template<class T> struct integer_reader<T, true> {
    static T value(typename std::make_unsigned<T>::type x) noexcept {
        return signed_from_bits<T>(x);
    }
};
}
template<class T, std::uint64_t Tag> struct integer_codec : detail::minimum_size<sizeof(T)> {
    static_assert(std::is_integral<T>::value && !std::is_same<T, bool>::value,
                  "wire integer must be a fixed-width integral type");
    using value_type = T;
    using bits_type = typename std::make_unsigned<T>::type;
    static constexpr std::uint64_t fingerprint() noexcept { return mix(hash_seed, Tag); }
    static bool size(measure& s, T) noexcept { return s.add(sizeof(T)); }
    static bool write(writer& w, T x) noexcept { return w.integer(static_cast<bits_type>(x)); }
    static bool read(reader& r, T& x) noexcept {
        bits_type bits = 0; if (!r.integer(bits)) return false;
        x = detail::integer_reader<T>::value(bits); return true;
    }
};
using u8 = integer_codec<std::uint8_t, 1>;
using u16 = integer_codec<std::uint16_t, 2>;
using u32 = integer_codec<std::uint32_t, 3>;
using u64 = integer_codec<std::uint64_t, 4>;
using i8 = integer_codec<std::int8_t, 5>;
using i16 = integer_codec<std::int16_t, 6>;
using i32 = integer_codec<std::int32_t, 7>;
using i64 = integer_codec<std::int64_t, 8>;
struct boolean : detail::minimum_size<1> {
    using value_type = bool;
    static constexpr std::uint64_t fingerprint() noexcept { return mix(hash_seed, 9); }
    static bool size(measure& s, bool) noexcept { return s.add(1); }
    static bool write(writer& w, bool x) noexcept { return w.integer<std::uint8_t>(x ? 1 : 0); }
    static bool read(reader& r, bool& x) noexcept {
        std::uint8_t b = 0; if (!r.integer(b)) return false;
        if (b > 1) return r.fail(errc::invalid_value);
        x = b != 0; return true;
    }
};
struct f64 : detail::minimum_size<8> {
    using value_type = double;
    static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559
                  && std::numeric_limits<double>::radix == 2
                  && std::numeric_limits<double>::digits == 53,
                  "wire f64 requires IEEE-754 binary64");
    static constexpr std::uint64_t fingerprint() noexcept { return mix(hash_seed, 10); }
    static bool size(measure& s, double) noexcept { return s.add(8); }
    static bool write(writer& w, double x) noexcept {
        std::uint64_t bits; std::memcpy(&bits, &x, 8); return w.integer(bits);
    }
    static bool read(reader& r, double& x) noexcept {
        std::uint64_t bits; if (!r.integer(bits)) return false;
        std::memcpy(&x, &bits, 8); return true;
    }
};
struct unit {};
struct none : detail::minimum_size<0> {
    using value_type = unit;
    static constexpr std::uint64_t fingerprint() noexcept { return mix(hash_seed, 11); }
    static bool size(measure&, unit) noexcept { return true; }
    static bool write(writer&, unit) noexcept { return true; }
    static bool read(reader&, unit&) noexcept { return true; }
};
// Blob is deliberately not text: embedded NUL/arbitrary bytes are valid.
template<std::size_t Maximum> struct blob : detail::minimum_size<4> {
    static_assert(Maximum <= (std::numeric_limits<std::uint32_t>::max)(), "blob bound exceeds wire length");
    using value_type = std::string;
    static constexpr std::uint64_t fingerprint() noexcept { return mix(mix(hash_seed, 20), Maximum); }
    static bool size(measure& s, const std::string& x) noexcept {
        if (x.size() > Maximum) return s.fail(errc::limit_exceeded);
        return s.add(4) && s.add(x.size());
    }
    static bool write(writer& w, const std::string& x) noexcept {
        return x.size() <= Maximum && w.integer(static_cast<std::uint32_t>(x.size())) && w.raw(x.data(), x.size());
    }
    static bool read(reader& r, std::string& x) {
        std::uint32_t n = 0; if (!r.integer(n)) return false;
        if (n > Maximum) return r.fail(errc::limit_exceeded);
        if (!r.require(n) || !r.charge(n)) return false;
        return r.string(x, n);
    }
};
template<class C, std::size_t N> struct array : detail::minimum_size<min_product(C::min_size, N)> {
    using value_type = std::array<typename C::value_type, N>;
    static constexpr std::uint64_t fingerprint() noexcept { return mix(mix(mix(hash_seed, 21), N), C::fingerprint()); }
    static bool size(measure& s, const value_type& x) noexcept {
        for (const auto& e : x) if (!C::size(s, e)) return false;
        return true;
    }
    static bool write(writer& w, const value_type& x) noexcept {
        for (const auto& e : x) if (!C::write(w, e)) return false;
        return true;
    }
    static bool read(reader& r, value_type& x) {
        depth_scope depth(r);
        if (!depth.entered || !r.elements(N) || !r.require(array::min_size)) return false;
        for (auto& e : x) if (!C::read(r, e)) return false;
        return true;
    }
};
template<class C, std::size_t Maximum> struct sequence : detail::minimum_size<4> {
    using element_type = typename C::value_type;
    using value_type = std::vector<element_type>;
    static_assert(!std::is_same<element_type, bool>::value, "vector<bool> is not a wire sequence; use u8");
    static_assert(Maximum <= (std::numeric_limits<std::uint32_t>::max)(), "sequence bound exceeds wire length");
    static constexpr std::uint64_t fingerprint() noexcept { return mix(mix(mix(hash_seed, 22), Maximum), C::fingerprint()); }
    static bool size(measure& s, const value_type& x) noexcept {
        if (x.size() > Maximum) return s.fail(errc::limit_exceeded);
        if (!s.add(4)) return false;
        for (const auto& e : x) if (!C::size(s, e)) return false;
        return true;
    }
    static bool write(writer& w, const value_type& x) noexcept {
        if (x.size() > Maximum || !w.integer(static_cast<std::uint32_t>(x.size()))) return false;
        for (const auto& e : x) if (!C::write(w, e)) return false;
        return true;
    }
    static bool read(reader& r, value_type& x) {
        depth_scope depth(r); if (!depth.entered) return false;
        std::uint32_t n = 0; if (!r.integer(n)) return false;
        if (n > Maximum) return r.fail(errc::limit_exceeded);
        if (!r.elements(n)) return false;
        if (C::min_size && n > r.remaining() / C::min_size) return r.fail(errc::malformed);
        if (n > (std::numeric_limits<std::size_t>::max)() / sizeof(element_type)) return r.fail(errc::limit_exceeded);
        if (!r.charge(static_cast<std::size_t>(n) * sizeof(element_type))) return false;
        x.resize(n);
        for (auto& e : x) if (!C::read(r, e)) return false;
        return true;
    }
};
template<class T> struct option { bool present = false; T value{}; };
template<class C> struct maybe : detail::minimum_size<1> {
    using value_type = option<typename C::value_type>;
    static constexpr std::uint64_t fingerprint() noexcept { return mix(mix(hash_seed, 23), C::fingerprint()); }
    static bool size(measure& s, const value_type& x) noexcept { return s.add(1) && (!x.present || C::size(s, x.value)); }
    static bool write(writer& w, const value_type& x) noexcept { return boolean::write(w, x.present) && (!x.present || C::write(w, x.value)); }
    static bool read(reader& r, value_type& x) {
        depth_scope depth(r);
        return depth.entered && boolean::read(r, x.present) && (!x.present || C::read(r, x.value));
    }
};

namespace detail {
template<class> struct missing_schema : std::false_type {};
}
template<class T> struct schema {
    static_assert(detail::missing_schema<T>::value,
                  "wire record requires a PORTAL_WIRE_RECORD declaration");
};
// Pointer-to-member type-checks the mapping without invoking getters or user code.
template<class T, std::uint32_t Id, class C, typename C::value_type T::*Pointer>
struct member : detail::field_identity<Id>, detail::minimum_size<C::min_size> {
    static_assert(Id != 0, "field id zero is reserved");
    static constexpr std::uint64_t fingerprint() noexcept { return mix(C::fingerprint(), Id); }
    static bool size(measure& s, const T& x) noexcept { return C::size(s, x.*Pointer); }
    static bool write(writer& w, const T& x) noexcept { return C::write(w, x.*Pointer); }
    static bool read(reader& r, T& x) { return C::read(r, x.*Pointer); }
};
struct end_field : detail::field_identity<0>, detail::minimum_size<0> {
    template<class T> static bool size(measure&, const T&) noexcept { return true; }
    template<class T> static bool write(writer&, const T&) noexcept { return true; }
    template<class T> static bool read(reader&, T&) noexcept { return true; }
};
namespace detail {
template<std::uint32_t Prev, class... F> struct ordered_fields;
template<std::uint32_t Prev> struct ordered_fields<Prev, end_field> : std::true_type {};
template<std::uint32_t Prev, class F, class... Rest> struct ordered_fields<Prev, F, Rest...>
    : std::integral_constant<bool, (F::id > Prev) && ordered_fields<F::id, Rest...>::value> {};
template<class... F> struct fields_meta;
template<> struct fields_meta<end_field> {
    static constexpr std::size_t minimum = 0;
    static constexpr std::uint64_t hash(std::uint64_t h) noexcept { return h; }
};
template<class F, class... Rest> struct fields_meta<F, Rest...> {
    static constexpr std::size_t minimum = min_sum(F::min_size, fields_meta<Rest...>::minimum);
    static constexpr std::uint64_t hash(std::uint64_t h) noexcept {
        return fields_meta<Rest...>::hash(mix(h, F::fingerprint()));
    }
};
}
template<class T, std::uint64_t Id, std::uint32_t Revision, class... Fields>
struct record_definition : detail::minimum_size<detail::fields_meta<Fields...>::minimum> {
    using value_type = T;
    static_assert(Id && Revision, "record id and revision must be nonzero");
    static_assert(detail::ordered_fields<0, Fields...>::value, "field ids must be unique, nonzero and increasing");
    static constexpr std::uint64_t fingerprint() noexcept {
        return detail::fields_meta<Fields...>::hash(mix(mix(mix(hash_seed, 30), Id), Revision));
    }
    static bool size(measure& s, const T& x) noexcept {
        bool ok = true; using swallow = int[];
        (void)swallow{0, (ok = ok && Fields::size(s, x), 0)...}; return ok;
    }
    static bool write(writer& w, const T& x) noexcept {
        bool ok = true; using swallow = int[];
        (void)swallow{0, (ok = ok && Fields::write(w, x), 0)...}; return ok;
    }
    static bool read(reader& r, T& x) {
        depth_scope depth(r);
        bool ok = depth.entered && r.require(record_definition::min_size); using swallow = int[];
        (void)swallow{0, (ok = ok && Fields::read(r, x), 0)...}; return ok;
    }
};
template<class T> struct record : schema<T> {};

// An owning result. T must be default constructible. Failure does not expose a
// partly decoded value: public decode helpers commit only on full validation.
template<class T> struct result {
    errc error = errc::ok;
    T value{};
    explicit operator bool() const noexcept { return error == errc::ok; }
};
template<class C> result<typename C::value_type> decode(view v, limits l = {}) {
    using T = typename C::value_type;
    result<T> out; T staged{}; reader r(v, l);
    if (!C::read(r, staged) || !r.empty()) {
        out.error = r.error() == errc::ok ? errc::malformed : r.error();
        return out;
    }
    out.value = std::move(staged); return out;
}
template<class C> errc encode_into(const typename C::value_type& x, bytes& out, limits l = {}) {
    measure s(l.max_frame_bytes); if (!C::size(s, x)) return s.error();
    out.resize(s.size()); writer w(out.data(), out.size());
    if (!C::write(w, x) || !w.empty()) { out.clear(); return errc::invalid_value; }
    return errc::ok;
}
}}

// Invoke at namespace scope, outside any user namespace. FIELDS(X, T) expands
// X(T, stable_id, member_name, codec_type) entries. Variadic tail permits codecs
// containing commas. A record maps existing members; it never owns an object ID.
#define PORTAL_WIRE_DETAIL_MEMBER(T, ID, NAME, ...) \
    ::portal::wire::member<T, ID, __VA_ARGS__, &T::NAME>,
#define PORTAL_WIRE_DETAIL_CHECK_MEMBER(T, ID, NAME, ...) \
    static_assert(::std::is_same<decltype(T::NAME), typename __VA_ARGS__::value_type>::value, \
                  "wire field C++ type must match codec value_type");
#define PORTAL_WIRE_RECORD(T, ID, REVISION, FIELDS) \
    FIELDS(PORTAL_WIRE_DETAIL_CHECK_MEMBER, T) \
    template<> struct portal::wire::schema<T> \
        : ::portal::wire::record_definition<T, ID, REVISION, \
            FIELDS(PORTAL_WIRE_DETAIL_MEMBER, T) ::portal::wire::end_field> {}

#endif
