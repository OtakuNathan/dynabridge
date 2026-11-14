#ifndef PORTAL_BACKENDS_RPC_H
#define PORTAL_BACKENDS_RPC_H

#include "../error_policy.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "../backend_base.h"

namespace portal {
    namespace rpc {
        enum class value_kind : std::uint8_t {
            undefined,
            signed_integer,
            unsigned_integer,
            real,
            boolean,
            string
        };

        class value {
        public:
            value() noexcept = default;

            static value signed_integer(std::int64_t number) noexcept {
                value result;
                result.kind_ = value_kind::signed_integer;
                std::memcpy(&result.bits_, &number, sizeof(number));
                return result;
            }

            static value unsigned_integer(std::uint64_t number) noexcept {
                value result;
                result.kind_ = value_kind::unsigned_integer;
                result.bits_ = number;
                return result;
            }

            static value real(double number) noexcept {
                value result;
                result.kind_ = value_kind::real;
                std::memcpy(&result.bits_, &number, sizeof(number));
                return result;
            }

            static value boolean(bool state) noexcept {
                value result;
                result.kind_ = value_kind::boolean;
                result.bits_ = state ? 1u : 0u;
                return result;
            }

            static value string(std::string text) {
                value result;
                result.kind_ = value_kind::string;
                result.text_ = std::move(text);
                return result;
            }

            static value borrowed_string(const std::string& text) noexcept {
                value result;
                result.kind_ = value_kind::string;
                result.borrowed_data_ = text.data();
                result.borrowed_size_ = text.size();
                result.borrowed_ = true;
                return result;
            }

            value_kind kind() const noexcept { return kind_; }

            std::int64_t as_signed() const noexcept {
                std::int64_t result = 0;
                std::memcpy(&result, &bits_, sizeof(result));
                return result;
            }

            std::uint64_t as_unsigned() const noexcept { return bits_; }

            double as_real() const noexcept {
                double result = 0;
                std::memcpy(&result, &bits_, sizeof(result));
                return result;
            }

            bool as_boolean() const noexcept { return bits_ != 0; }
            const char* string_data() const noexcept {
                return borrowed_ ? borrowed_data_ : text_.data();
            }
            std::size_t string_size() const noexcept {
                return borrowed_ ? borrowed_size_ : text_.size();
            }
            std::string copy_string() const {
                return string_size() == 0
                    ? std::string()
                    : std::string(string_data(), string_size());
            }
            std::string take_string() {
                return borrowed_ ? copy_string() : std::move(text_);
            }

        private:
            value_kind kind_ = value_kind::undefined;
            std::uint64_t bits_ = 0;
            std::string text_;
            const char* borrowed_data_ = nullptr;
            std::size_t borrowed_size_ = 0;
            bool borrowed_ = false;
        };

        class protocol_error : public std::runtime_error {
        public:
            explicit protocol_error(const std::string& message)
                : std::runtime_error(message) {
            }
        };

        class remote_error : public std::runtime_error {
        public:
            explicit remote_error(const std::string& message)
                : std::runtime_error(message) {
            }
        };

        using bytes = std::vector<std::uint8_t>;

        namespace detail {
            constexpr std::uint32_t request_magic = 0x43505244u;
            constexpr std::uint32_t response_magic = 0x52505244u;
            constexpr std::uint8_t protocol_version = 1;

            class writer {
            public:
                explicit writer(std::size_t capacity = 0) {
                    data_.reserve(capacity);
                }

                // An explicit reuse path. The original owning writer is unchanged.
                writer(bytes&& storage, std::size_t capacity)
                    : data_(std::move(storage)) {
                    data_.clear();
                    data_.reserve(capacity);
                }

                template <typename UInt>
                void integer(UInt number) {
                    static_assert(std::is_unsigned<UInt>::value, "wire integers must be unsigned");
                    if (sizeof(UInt) == 1) {
                        data_.push_back(static_cast<std::uint8_t>(number));
                        return;
                    }
                    std::uint8_t encoded[sizeof(UInt)];
                    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
                        encoded[i] = static_cast<std::uint8_t>(number >> (i * 8));
                    }
                    data_.insert(data_.end(), encoded, encoded + sizeof(UInt));
                }

                void raw(const char* data, std::size_t size) {
                    if (size > std::numeric_limits<std::uint32_t>::max()) {
                        PORTAL_RAISE(protocol_error("RPC string is too large"));
                    }
                    integer<std::uint32_t>(static_cast<std::uint32_t>(size));
                    if (size != 0) {
                        data_.insert(data_.end(), data, data + size);
                    }
                }

                void raw(const std::string& text) {
                    raw(text.data(), text.size());
                }

                bytes finish() { return std::move(data_); }

            private:
                bytes data_;
            };

            class reader {
            public:
                explicit reader(const bytes& data) noexcept
                    : current_(data.data()), remaining_(data.size()) {
                }

                template <typename UInt>
                UInt integer() {
                    static_assert(std::is_unsigned<UInt>::value, "wire integers must be unsigned");
                    if (!require(sizeof(UInt))) return 0;
                    UInt result = 0;
                    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
                        result |= static_cast<UInt>(current_[i]) << (i * 8);
                    }
                    current_ += sizeof(UInt);
                    remaining_ -= sizeof(UInt);
                    return result;
                }

                std::string raw() {
                    const auto size = integer<std::uint32_t>();
                    if (!require(size)) return {};
                    std::string result(
                        reinterpret_cast<const char*>(current_),
                        reinterpret_cast<const char*>(current_ + size));
                    current_ += size;
                    remaining_ -= size;
                    return result;
                }

                bool empty() const noexcept { return remaining_ == 0; }
                bool valid() const noexcept { return valid_; }
                void invalidate() noexcept { valid_ = false; }
                std::size_t remaining() const noexcept { return remaining_; }

            private:
                bool require(std::size_t size) noexcept {
                    if (!valid_ || size > remaining_) { valid_ = false; return false; }
                    return true;
                }

                const std::uint8_t* current_;
                std::size_t remaining_;
                bool valid_ = true;
            };

            inline void encode_value(writer& out, const value& input) {
                out.integer<std::uint8_t>(static_cast<std::uint8_t>(input.kind()));
                switch (input.kind()) {
                case value_kind::undefined:
                    return;
                case value_kind::signed_integer: {
                    std::uint64_t bits = 0;
                    const auto number = input.as_signed();
                    std::memcpy(&bits, &number, sizeof(bits));
                    out.integer<std::uint64_t>(bits);
                    return;
                }
                case value_kind::unsigned_integer:
                    out.integer<std::uint64_t>(input.as_unsigned());
                    return;
                case value_kind::real: {
                    std::uint64_t bits = 0;
                    const auto number = input.as_real();
                    std::memcpy(&bits, &number, sizeof(bits));
                    out.integer<std::uint64_t>(bits);
                    return;
                }
                case value_kind::boolean:
                    out.integer<std::uint8_t>(input.as_boolean() ? 1u : 0u);
                    return;
                case value_kind::string:
                    out.raw(input.string_data(), input.string_size());
                    return;
                }
                PORTAL_RAISE(protocol_error("unknown RPC value kind"));
            }

            inline std::size_t checked_add(std::size_t left, std::size_t right) {
                if (right > std::numeric_limits<std::size_t>::max() - left) {
                    PORTAL_RAISE(protocol_error("RPC frame size overflow"));
                }
                return left + right;
            }

            inline std::size_t encoded_size(const value& input) {
                switch (input.kind()) {
                case value_kind::undefined:
                    return 1;
                case value_kind::signed_integer:
                case value_kind::unsigned_integer:
                case value_kind::real:
                    return 9;
                case value_kind::boolean:
                    return 2;
                case value_kind::string:
                    if (input.string_size() > std::numeric_limits<std::uint32_t>::max()) {
                        PORTAL_RAISE(protocol_error("RPC string is too large"));
                    }
                    return checked_add(5, input.string_size());
                }
                return 1;
            }

            inline value decode_value(reader& input) {
                const auto kind = static_cast<value_kind>(input.integer<std::uint8_t>());
                switch (kind) {
                case value_kind::undefined:
                    return value{};
                case value_kind::signed_integer: {
                    const auto bits = input.integer<std::uint64_t>();
                    std::int64_t number = 0;
                    std::memcpy(&number, &bits, sizeof(number));
                    return value::signed_integer(number);
                }
                case value_kind::unsigned_integer:
                    return value::unsigned_integer(input.integer<std::uint64_t>());
                case value_kind::real: {
                    const auto bits = input.integer<std::uint64_t>();
                    double number = 0;
                    std::memcpy(&number, &bits, sizeof(number));
                    return value::real(number);
                }
                case value_kind::boolean:
                    return value::boolean(input.integer<std::uint8_t>() != 0);
                case value_kind::string:
                    return value::string(input.raw());
                }
                input.invalidate();
                return {};
            }

            inline std::uint64_t symbol_id(const char* name) noexcept {
                std::uint64_t hash = 14695981039346656037ull;
                for (const auto* current = reinterpret_cast<const unsigned char*>(name);
                     *current != 0; ++current) {
                    hash ^= *current;
                    hash *= 1099511628211ull;
                }
                return hash;
            }

            struct request {
                std::uint64_t method = 0;
                std::vector<value> args;
            };

            inline bytes encode_request(std::uint64_t method, const std::vector<value>& args) {
                if (args.size() > std::numeric_limits<std::uint32_t>::max()) {
                    PORTAL_RAISE(protocol_error("too many RPC arguments"));
                }
                std::size_t frame_size = 17;
                for (const auto& arg : args) {
                    frame_size = checked_add(frame_size, encoded_size(arg));
                }
                writer out(frame_size);
                out.integer<std::uint32_t>(request_magic);
                out.integer<std::uint8_t>(protocol_version);
                out.integer<std::uint64_t>(method);
                out.integer<std::uint32_t>(static_cast<std::uint32_t>(args.size()));
                for (const auto& arg : args) {
                    encode_value(out, arg);
                }
                return out.finish();
            }

            template <typename... Values>
            bytes encode_request_values(std::uint64_t method, const Values&... args) {
                static_assert(sizeof...(Values) <= std::numeric_limits<std::uint32_t>::max(),
                    "too many RPC arguments");
                std::size_t frame_size = 17;
                using swallow = int[];
                (void)swallow{0, (frame_size = checked_add(frame_size, encoded_size(args)), 0)...};
                writer out(frame_size);
                out.integer<std::uint32_t>(request_magic);
                out.integer<std::uint8_t>(protocol_version);
                out.integer<std::uint64_t>(method);
                out.integer<std::uint32_t>(static_cast<std::uint32_t>(sizeof...(Values)));
                (void)swallow{0, (encode_value(out, args), 0)...};
                return out.finish();
            }

            // Output storage must not contain any borrowed string input. On an
            // allocation exception output remains valid, but its old bytes/capacity
            // are not preserved. No bytes are written beyond vector::size().
            template <typename... Values>
            void encode_request_values_into(bytes& output, std::uint64_t method,
                                             const Values&... args) {
                static_assert(sizeof...(Values) <= std::numeric_limits<std::uint32_t>::max(),
                    "too many RPC arguments");
                std::size_t frame_size = 17;
                using swallow = int[];
                (void)swallow{0, (frame_size = checked_add(frame_size, encoded_size(args)), 0)...};
                writer out(std::move(output), frame_size);
                out.integer<std::uint32_t>(request_magic);
                out.integer<std::uint8_t>(protocol_version);
                out.integer<std::uint64_t>(method);
                out.integer<std::uint32_t>(static_cast<std::uint32_t>(sizeof...(Values)));
                (void)swallow{0, (encode_value(out, args), 0)...};
                output = out.finish();
            }

            inline void encode_response_into(bytes& output, const value& result) {
                const auto size = checked_add(7, encoded_size(result));
                writer out(std::move(output), size);
                out.integer<std::uint32_t>(response_magic);
                out.integer<std::uint8_t>(protocol_version);
                out.integer<std::uint8_t>(0);
                encode_value(out, result);
                output = out.finish();
            }

            inline void encode_error_into(bytes& output, const std::string& message) {
                if (message.size() > std::numeric_limits<std::uint32_t>::max())
                    PORTAL_RAISE(protocol_error("RPC error message is too large"));
                const auto size = checked_add(10, message.size());
                writer out(std::move(output), size);
                out.integer<std::uint32_t>(response_magic);
                out.integer<std::uint8_t>(protocol_version);
                out.integer<std::uint8_t>(1);
                out.raw(message);
                output = out.finish();
            }

            // Private scratch decoder: unlike try_decode_request, a failed parse
            // may leave partial scratch. The enclosing workspace lease destroys
            // it before return; no binder is called before full frame validation.
            inline bool decode_request_scratch(const bytes& frame, request& scratch) {
                reader input(frame);
                if (input.integer<std::uint32_t>() != request_magic
                    || input.integer<std::uint8_t>() != protocol_version) return false;
                scratch.method = input.integer<std::uint64_t>();
                const auto argc = input.integer<std::uint32_t>();
                if (!input.valid() || argc > input.remaining()) return false;
                scratch.args.reserve(argc);
                for (std::uint32_t i = 0; i < argc; ++i) {
                    auto item = decode_value(input);
                    if (!input.valid()) return false;
                    scratch.args.emplace_back(std::move(item));
                }
                return input.valid() && input.empty();
            }

            // Malformed input is an ordinary result, independent of C++ exceptions.
            // General allocation failure follows the program's allocator policy.
            inline bool try_decode_request(const bytes& frame, request& result) {
                reader input(frame);
                if (input.integer<std::uint32_t>() != request_magic
                    || input.integer<std::uint8_t>() != protocol_version) return false;
                request staged;
                staged.method = input.integer<std::uint64_t>();
                const auto argc = input.integer<std::uint32_t>();
                if (!input.valid() || argc > input.remaining()) return false;
                staged.args.reserve(argc);
                for (std::uint32_t i = 0; i < argc; ++i) {
                    auto item = decode_value(input);
                    if (!input.valid()) return false;
                    staged.args.emplace_back(std::move(item));
                }
                if (!input.valid() || !input.empty()) return false;
                result = std::move(staged);
                return true;
            }
            inline request decode_request(const bytes& frame) {
                request result;
                if (!try_decode_request(frame, result))
                    PORTAL_RAISE(protocol_error("invalid RPC request frame"));
                return result;
            }

            inline bytes encode_response(const value& result) {
                writer out(checked_add(7, encoded_size(result)));
                out.integer<std::uint32_t>(response_magic);
                out.integer<std::uint8_t>(protocol_version);
                out.integer<std::uint8_t>(0);
                encode_value(out, result);
                return out.finish();
            }

            inline bytes encode_error(const std::string& message) {
                if (message.size() > std::numeric_limits<std::uint32_t>::max()) {
                    PORTAL_RAISE(protocol_error("RPC error message is too large"));
                }
                writer out(checked_add(10, message.size()));
                out.integer<std::uint32_t>(response_magic);
                out.integer<std::uint8_t>(protocol_version);
                out.integer<std::uint8_t>(1);
                out.raw(message);
                return out.finish();
            }

            enum class response_status { ok, malformed, remote_error };
            inline response_status try_decode_response(const bytes& frame, value& result,
                                                       std::string& error) {
                reader input(frame);
                if (input.integer<std::uint32_t>() != response_magic
                    || input.integer<std::uint8_t>() != protocol_version) return response_status::malformed;
                const auto tag = input.integer<std::uint8_t>();
                if (!input.valid() || tag > 1) return response_status::malformed;
                if (tag == 1) {
                    auto message = input.raw();
                    if (!input.valid() || !input.empty()) return response_status::malformed;
                    error = std::move(message);
                    return response_status::remote_error;
                }
                auto staged = decode_value(input);
                if (!input.valid() || !input.empty()) return response_status::malformed;
                result = std::move(staged);
                return response_status::ok;
            }
            inline value decode_response(const bytes& frame) {
                value result; std::string error;
                const auto code = try_decode_response(frame, result, error);
                if (code == response_status::remote_error) PORTAL_RAISE(remote_error(error));
                if (code != response_status::ok) PORTAL_RAISE(protocol_error("invalid RPC response frame"));
                return result;
            }

            template <typename Binder, typename Signature>
            struct binder_call;

            template <typename Binder, typename R, typename... Args>
            struct binder_call<Binder, R(Args...)> {
                template <std::size_t... Indices>
                static optional<value> call(Binder& binder, std::vector<value>& args,
                    std::index_sequence<Indices...>) {
                    return binder.call_optional(args[Indices]...);
                }
            };

            template <typename Binder, typename... Args>
            struct binder_call<Binder, void(Args...)> {
                template <std::size_t... Indices>
                static optional<value> call(Binder& binder, std::vector<value>& args,
                    std::index_sequence<Indices...>) {
                    return binder.call_optional(args[Indices]...);
                }
            };

            struct vector_accessor {
                constexpr static std::size_t static_arity = dynamic_arity;
                const std::vector<value>* args = nullptr;

                template <std::size_t I>
                const value& get() const { return (*args)[I]; }
            };
            // The router exclusively owns decoded request arguments. Only a
            // library binder may consume them, after candidate-wide probing.
            struct consuming_vector_accessor {
                constexpr static std::size_t static_arity = dynamic_arity;
                std::vector<value>* args = nullptr;

                template <std::size_t I>
                value& get() const { return (*args)[I]; }
            };
        }

        // A worker-confined cache of EMPTY argument storage. Never share one
        // workspace concurrently. A nested synchronous dispatch is supported:
        // each active call takes ownership of its vector, leaving an empty cache
        // for a nested call. All strings remain owning, as in dispatch().
        class dispatch_workspace {
            friend class router;
            std::vector<value> cache_;
            const std::size_t max_retained_args_;

            class lease {
                dispatch_workspace& owner_;
            public:
                detail::request request;
                explicit lease(dispatch_workspace& owner) noexcept : owner_(owner) {
                    request.args.swap(owner_.cache_);
                }
                lease(const lease&) = delete;
                lease& operator=(const lease&) = delete;
                ~lease() noexcept {
                    request.args.clear(); // Do not retain payload allocations.
                    if (request.args.capacity() <= owner_.max_retained_args_
                        && request.args.capacity() > owner_.cache_.capacity()) {
                        request.args.swap(owner_.cache_);
                    }
                }
            };

        public:
            explicit dispatch_workspace(std::size_t max_retained_args = 64) noexcept
                : max_retained_args_(max_retained_args) {}
            dispatch_workspace(const dispatch_workspace&) = delete;
            dispatch_workspace& operator=(const dispatch_workspace&) = delete;
            // The workspace itself must outlive all dispatches using it.
            std::size_t cached_argument_capacity() const noexcept { return cache_.capacity(); }
            void release_cached_storage() noexcept { std::vector<value>().swap(cache_); }
        };

        // CONTRACT (thread safety): the router is not synchronized.
        // All define() calls must complete (single thread or externally
        // synchronized) before dispatch() may be called from any thread —
        // "define-then-serve". Concurrent define+dispatch is a data race.
        // Once defined, dispatch() itself is safe to call concurrently.
        class router {
            class route {
            public:
                route() noexcept = default;
                route(const route&) = delete;
                route& operator=(const route&) = delete;

                route(route&& other) noexcept
                    : id_(other.id_), name_(std::move(other.name_)), state_(other.state_),
                      invoke_(other.invoke_), destroy_(other.destroy_) {
                    other.state_ = nullptr;
                }

                route& operator=(route&& other) noexcept {
                    if (this != &other) {
                        reset();
                        id_ = other.id_;
                        name_ = std::move(other.name_);
                        state_ = other.state_;
                        invoke_ = other.invoke_;
                        destroy_ = other.destroy_;
                        other.state_ = nullptr;
                    }
                    return *this;
                }

                ~route() { reset(); }

                template <typename Binder>
                static route make(std::uint64_t id, std::string name, Binder binder) {
                    route result;
                    result.id_ = id;
                    result.name_ = std::move(name);
                    result.state_ = new Binder(std::move(binder));
                    result.invoke_ = &invoke_binder<Binder>;
                    result.destroy_ = &destroy_binder<Binder>;
                    return result;
                }

                std::uint64_t id() const noexcept { return id_; }
                const std::string& name() const noexcept { return name_; }

                optional<value> invoke(std::vector<value>& args) {
                    return invoke_(state_, args);
                }

            private:
                template <typename Binder>
                static optional<value> invoke_binder(void* state, std::vector<value>& args) {
                    return invoke_binder_impl(
                        *static_cast<Binder*>(state), args,
                        is_transactional_export_binder<Binder>{});
                }

                template <typename Binder>
                static optional<value> invoke_binder_impl(Binder& binder, std::vector<value>& args,
                    std::true_type) {
                    return binder.dispatch_optional(args.size(), detail::consuming_vector_accessor{&args});
                }

                template <typename Binder>
                static optional<value> invoke_binder_impl(Binder& binder, std::vector<value>& args,
                    std::false_type) {
                    return invoke_legacy_binder(binder, args, is_export_overload_binder<Binder>{});
                }

                template <typename Binder>
                static optional<value> invoke_legacy_binder(Binder& binder, std::vector<value>& args,
                    std::false_type) {
                    using signature_t = typename Binder::signature_t;
                    return invoke_typed(binder, args, type_identity<signature_t>{});
                }

                template <typename Binder>
                static optional<value> invoke_legacy_binder(Binder& binder, std::vector<value>& args,
                    std::true_type) {
                    return binder.dispatch_optional(args.size(), detail::vector_accessor{&args});
                }

                template <typename Binder, typename R, typename... Args>
                static optional<value> invoke_typed(Binder& binder, std::vector<value>& args,
                    type_identity<R(Args...)>) {
                    if (args.size() != sizeof...(Args)) {
                        return {};
                    }
                    return detail::binder_call<Binder, R(Args...)>::call(
                        binder, args, std::index_sequence_for<Args...>{});
                }

                template <typename Binder>
                static void destroy_binder(void* state) noexcept {
                    delete static_cast<Binder*>(state);
                }

                void reset() noexcept {
                    if (state_ != nullptr) {
                        destroy_(state_);
                        state_ = nullptr;
                    }
                }

                std::uint64_t id_ = 0;
                std::string name_;
                void* state_ = nullptr;
                optional<value> (*invoke_)(void*, std::vector<value>&) = nullptr;
                void (*destroy_)(void*) = nullptr;
            };

        public:
            template <typename Binder>
            void define(const char* name, Binder binder) {
                const auto id = detail::symbol_id(name);
                const auto existing = std::lower_bound(
                    routes_.begin(), routes_.end(), id,
                    [](const route& candidate, std::uint64_t value) {
                        return candidate.id() < value;
                    });
                if (existing != routes_.end() && existing->id() == id) {
                    if (existing->name() != name) {
                        PORTAL_RAISE(protocol_error("RPC symbol hash collision"));
                    }
                    *existing = route::make(id, name, std::move(binder));
                    return;
                }
                routes_.insert(existing, route::make(id, name, std::move(binder)));
            }

            bytes dispatch(const bytes& frame) {
#if PORTAL_HAS_EXCEPTIONS
                try {
#endif
                    detail::request request;
                    if(!detail::try_decode_request(frame,request)) return detail::encode_error("invalid RPC request frame");
                    const auto candidate=std::lower_bound(routes_.begin(),routes_.end(),request.method,
                        [](const route& entry,std::uint64_t value) {return entry.id()<value;});
                    if(candidate==routes_.end() || candidate->id()!=request.method) return detail::encode_error("unknown RPC method");
                    auto result=candidate->invoke(request.args);
                    if(!result) return detail::encode_error("RPC argument conversion or arity mismatch");
                    return detail::encode_response(*result);
#if PORTAL_HAS_EXCEPTIONS
                } catch(const std::exception& error) {return detail::encode_error(error.what());}
                  catch(...) {return detail::encode_error("unknown RPC server failure");}
#endif
            }

            // Synchronous, owning decode with reusable storage. The router's
            // registration and callable concurrency contracts are unchanged.
            // frame and output may be the same vector: parsing finishes before
            // output is modified, and decoded strings do not borrow frame bytes.
            void dispatch_into(const bytes& frame, bytes& output, dispatch_workspace& workspace) {
                dispatch_workspace::lease active(workspace);
#if PORTAL_HAS_EXCEPTIONS
                try {
#endif
                    auto& request = active.request;
                    if (!detail::decode_request_scratch(frame, request)) {
                        detail::encode_error_into(output, "invalid RPC request frame");
                        return;
                    }
                    const auto candidate = std::lower_bound(routes_.begin(), routes_.end(), request.method,
                        [](const route& entry, std::uint64_t id) { return entry.id() < id; });
                    if (candidate == routes_.end() || candidate->id() != request.method) {
                        detail::encode_error_into(output, "unknown RPC method");
                        return;
                    }
                    auto result = candidate->invoke(request.args);
                    if (!result) {
                        detail::encode_error_into(output, "RPC argument conversion or arity mismatch");
                        return;
                    }
                    detail::encode_response_into(output, *result);
#if PORTAL_HAS_EXCEPTIONS
                } catch (const std::exception& error) { detail::encode_error_into(output, error.what()); }
                  catch (...) { detail::encode_error_into(output, "unknown RPC server failure"); }
#endif
            }

            std::size_t size() const noexcept { return routes_.size(); }

        private:
            std::vector<route> routes_;
        };

        class loopback_transport {
        public:
            explicit loopback_transport(router& server) noexcept
                : server_(&server) {
            }

            bytes round_trip(const bytes& request) { return server_->dispatch(request); }

        private:
            router* server_;
        };
    }

    struct rpc_backend : backend_base<rpc_backend> {
        using export_probe_policy = rpc_export_probe_policy;
        using dynamic_value_t = rpc::value;
        using module_t = rpc::router;

        template <typename T>
        struct converter;

        struct export_context_t {
            using backend_t = rpc_backend;
        };

        template <typename Transport>
        struct import_context_t {
            using backend_t = rpc_backend;

            import_context_t(Transport& transport, std::uint64_t method) noexcept
                : transport_(&transport), method_(method) {
            }

            Transport* transport_;
            std::uint64_t method_;
        };

        template <typename Symbol, typename Context, typename Transport>
        static Context import_impl(Transport& transport, const char* name) {
            return Context(transport, rpc::detail::symbol_id(name));
        }

        template <typename Context, typename Binder>
        static void define_impl(Context&, module_t& module, const char* name, Binder binder) {
            module.define(name, std::move(binder));
        }

        template <typename Context>
        static dynamic_value_t undefined(Context&) noexcept {
            return dynamic_value_t{};
        }

        template <typename R, std::enable_if_t<!is_void_v<R>>* = nullptr,
            typename Context, typename... Args>
        static R invoke(Context& ctx, no_receiver_t, Args&&... args) {
            return invoke_impl<R>(
                ctx,
                no_receiver_t{},
                to_cast<typename std::decay<Args>::type>(
                    ctx, std::forward<Args>(args))...);
        }

        template <typename R, std::enable_if_t<is_void_v<R>>* = nullptr,
            typename Context, typename... Args>
        static void invoke(Context& ctx, no_receiver_t, Args&&... args) {
            invoke_impl<R>(
                ctx,
                no_receiver_t{},
                to_cast<typename std::decay<Args>::type>(
                    ctx, std::forward<Args>(args))...);
        }

        template <typename R, std::enable_if_t<!is_void_v<R>>* = nullptr,
            typename Transport, typename... Args>
        static R invoke_impl(import_context_t<Transport>& ctx, no_receiver_t, Args... args) {
            auto response = ctx.transport_->round_trip(
                rpc::detail::encode_request_values(ctx.method_, args...));
            return from_cast<R>(ctx, rpc::detail::decode_response(response));
        }

        template <typename R, std::enable_if_t<is_void_v<R>>* = nullptr,
            typename Transport, typename... Args>
        static void invoke_impl(import_context_t<Transport>& ctx, no_receiver_t, Args... args) {
            auto response = ctx.transport_->round_trip(
                rpc::detail::encode_request_values(ctx.method_, args...));
            (void)rpc::detail::decode_response(response);
        }
    };

    template <>
    struct rpc_backend::converter<int> {
        template <typename Context>
        static bool probe(Context&, const dynamic_value_t& value) noexcept {
            return value.kind() == rpc::value_kind::signed_integer
                && value.as_signed() >= std::numeric_limits<int>::min()
                && value.as_signed() <= std::numeric_limits<int>::max();
        }

        template <typename Context>
        static dynamic_value_t to(Context&, int value) noexcept {
            return dynamic_value_t::signed_integer(value);
        }

        template <typename Context>
        static optional<int> from(Context&, const dynamic_value_t& value) noexcept {
            if (value.kind() != rpc::value_kind::signed_integer
                    || value.as_signed() < std::numeric_limits<int>::min()
                    || value.as_signed() > std::numeric_limits<int>::max()) {
                return optional<int>();
            }
            return optional<int>(static_cast<int>(value.as_signed()));
        }
    };

    template <>
    struct rpc_backend::converter<unsigned> {
        template <typename Context>
        static bool probe(Context&, const dynamic_value_t& value) noexcept {
            return value.kind() == rpc::value_kind::unsigned_integer
                && value.as_unsigned() <= std::numeric_limits<unsigned>::max();
        }

        template <typename Context>
        static dynamic_value_t to(Context&, unsigned value) noexcept {
            return dynamic_value_t::unsigned_integer(value);
        }

        template <typename Context>
        static optional<unsigned> from(Context&, const dynamic_value_t& value) noexcept {
            if (value.kind() != rpc::value_kind::unsigned_integer
                    || value.as_unsigned() > std::numeric_limits<unsigned>::max()) {
                return optional<unsigned>();
            }
            return optional<unsigned>(static_cast<unsigned>(value.as_unsigned()));
        }
    };

    template <>
    struct rpc_backend::converter<double> {
        template <typename Context>
        static bool probe(Context&, const dynamic_value_t& value) noexcept {
            return value.kind() == rpc::value_kind::real;
        }

        template <typename Context>
        static dynamic_value_t to(Context&, double value) noexcept {
            return dynamic_value_t::real(value);
        }

        template <typename Context>
        static optional<double> from(Context&, const dynamic_value_t& value) noexcept {
            if (value.kind() != rpc::value_kind::real) {
                return optional<double>();
            }
            return optional<double>(value.as_real());
        }
    };

    template <>
    struct rpc_backend::converter<bool> {
        template <typename Context>
        static bool probe(Context&, const dynamic_value_t& value) noexcept {
            return value.kind() == rpc::value_kind::boolean;
        }

        template <typename Context>
        static dynamic_value_t to(Context&, bool value) noexcept {
            return dynamic_value_t::boolean(value);
        }

        template <typename Context>
        static optional<bool> from(Context&, const dynamic_value_t& value) noexcept {
            if (value.kind() != rpc::value_kind::boolean) {
                return optional<bool>();
            }
            return optional<bool>(value.as_boolean());
        }
    };

    template <>
    struct rpc_backend::converter<std::string> {
        template <typename Context>
        static bool probe(Context&, const dynamic_value_t& value) noexcept {
            return value.kind() == rpc::value_kind::string;
        }

        template <typename Context>
        static dynamic_value_t to(Context&, const std::string& value) noexcept {
            return dynamic_value_t::borrowed_string(value);
        }

        template <typename Context>
        static dynamic_value_t to(Context&, std::string&& value) PORTAL_NOEXCEPT_IF_DISABLED {
            return dynamic_value_t::string(std::move(value));
        }

        template <typename Context>
        static optional<std::string> from(Context&, const dynamic_value_t& value) PORTAL_NOEXCEPT_IF_DISABLED {
            if (value.kind() != rpc::value_kind::string) {
                return optional<std::string>();
            }
            return optional<std::string>(value.copy_string());
        }

        template <typename Context>
        static optional<std::string> from(Context&, dynamic_value_t& value) PORTAL_NOEXCEPT_IF_DISABLED {
            if (value.kind() != rpc::value_kind::string) {
                return optional<std::string>();
            }
            return optional<std::string>(value.take_string());
        }

        template <typename Context>
        static optional<std::string> from(Context&, dynamic_value_t&& value) PORTAL_NOEXCEPT_IF_DISABLED {
            if (value.kind() != rpc::value_kind::string) {
                return optional<std::string>();
            }
            return optional<std::string>(value.take_string());
        }
    };
}

#endif
