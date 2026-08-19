#ifndef DYNABRIDGE_BACKENDS_RPC_H
#define DYNABRIDGE_BACKENDS_RPC_H

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

namespace dynabridge {
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

                template <typename UInt>
                void integer(UInt number) {
                    static_assert(std::is_unsigned<UInt>::value, "wire integers must be unsigned");
                    for (std::size_t i = 0; i < sizeof(UInt); ++i) {
                        data_.push_back(static_cast<std::uint8_t>(number >> (i * 8)));
                    }
                }

                void raw(const char* data, std::size_t size) {
                    if (size > std::numeric_limits<std::uint32_t>::max()) {
                        throw protocol_error("RPC string is too large");
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
                    require(sizeof(UInt));
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
                    require(size);
                    std::string result(
                        reinterpret_cast<const char*>(current_),
                        reinterpret_cast<const char*>(current_ + size));
                    current_ += size;
                    remaining_ -= size;
                    return result;
                }

                bool empty() const noexcept { return remaining_ == 0; }
                std::size_t remaining() const noexcept { return remaining_; }

            private:
                void require(std::size_t size) {
                    if (size > remaining_) {
                        throw protocol_error("truncated RPC frame");
                    }
                }

                const std::uint8_t* current_;
                std::size_t remaining_;
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
                throw protocol_error("unknown RPC value kind");
            }

            inline std::size_t checked_add(std::size_t left, std::size_t right) {
                if (right > std::numeric_limits<std::size_t>::max() - left) {
                    throw protocol_error("RPC frame size overflow");
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
                        throw protocol_error("RPC string is too large");
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
                throw protocol_error("unknown RPC value kind");
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
                    throw protocol_error("too many RPC arguments");
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

            inline request decode_request(const bytes& frame) {
                reader input(frame);
                if (input.integer<std::uint32_t>() != request_magic
                        || input.integer<std::uint8_t>() != protocol_version) {
                    throw protocol_error("invalid RPC request header");
                }
                request result;
                result.method = input.integer<std::uint64_t>();
                const auto argc = input.integer<std::uint32_t>();
                if (argc > input.remaining()) {
                    throw protocol_error("RPC argument count exceeds the frame size");
                }
                result.args.reserve(argc);
                for (std::uint32_t i = 0; i < argc; ++i) {
                    result.args.emplace_back(decode_value(input));
                }
                if (!input.empty()) {
                    throw protocol_error("trailing data in RPC request");
                }
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
                    throw protocol_error("RPC error message is too large");
                }
                writer out(checked_add(10, message.size()));
                out.integer<std::uint32_t>(response_magic);
                out.integer<std::uint8_t>(protocol_version);
                out.integer<std::uint8_t>(1);
                out.raw(message);
                return out.finish();
            }

            inline value decode_response(const bytes& frame) {
                reader input(frame);
                if (input.integer<std::uint32_t>() != response_magic
                        || input.integer<std::uint8_t>() != protocol_version) {
                    throw protocol_error("invalid RPC response header");
                }
                if (input.integer<std::uint8_t>() != 0) {
                    throw remote_error(input.raw());
                }
                auto result = decode_value(input);
                if (!input.empty()) {
                    throw protocol_error("trailing data in RPC response");
                }
                return result;
            }

            template <typename Binder, typename Signature>
            struct binder_call;

            template <typename Binder, typename R, typename... Args>
            struct binder_call<Binder, R(Args...)> {
                template <std::size_t... Indices>
                static value call(Binder& binder, std::vector<value>& args,
                    std::index_sequence<Indices...>) {
                    return binder(args[Indices]...);
                }
            };

            template <typename Binder, typename... Args>
            struct binder_call<Binder, void(Args...)> {
                template <std::size_t... Indices>
                static value call(Binder& binder, std::vector<value>& args,
                    std::index_sequence<Indices...>) {
                    binder(args[Indices]...);
                    return value{};
                }
            };

            struct vector_accessor {
                constexpr static std::size_t static_arity = dynamic_arity;
                const std::vector<value>* args = nullptr;

                template <std::size_t I>
                const value& get() const { return (*args)[I]; }
            };
        }

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

                value invoke(std::vector<value>& args) {
                    return invoke_(state_, args);
                }

            private:
                template <typename Binder>
                static value invoke_binder(void* state, std::vector<value>& args) {
                    return invoke_binder_impl(
                        *static_cast<Binder*>(state), args,
                        is_export_overload_binder<Binder>{});
                }

                template <typename Binder>
                static value invoke_binder_impl(Binder& binder, std::vector<value>& args,
                    std::false_type) {
                    using signature_t = typename Binder::signature_t;
                    return invoke_typed(binder, args, type_identity<signature_t>{});
                }

                template <typename Binder>
                static value invoke_binder_impl(Binder& binder, std::vector<value>& args,
                    std::true_type) {
                    return binder.dispatch(args.size(), detail::vector_accessor{&args});
                }

                template <typename Binder, typename R, typename... Args>
                static value invoke_typed(Binder& binder, std::vector<value>& args,
                    type_identity<R(Args...)>) {
                    if (args.size() != sizeof...(Args)) {
                        throw bad_conversion("RPC argument count does not match the exported signature");
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
                value (*invoke_)(void*, std::vector<value>&) = nullptr;
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
                        throw protocol_error("RPC symbol hash collision");
                    }
                    *existing = route::make(id, name, std::move(binder));
                    return;
                }
                routes_.insert(existing, route::make(id, name, std::move(binder)));
            }

            bytes dispatch(const bytes& frame) {
                try {
                    auto request = detail::decode_request(frame);
                    const auto candidate = std::lower_bound(
                        routes_.begin(), routes_.end(), request.method,
                        [](const route& entry, std::uint64_t value) {
                            return entry.id() < value;
                        });
                    if (candidate != routes_.end() && candidate->id() == request.method) {
                        return detail::encode_response(candidate->invoke(request.args));
                    }
                    return detail::encode_error("unknown RPC method");
                } catch (const std::exception& error) {
                    return detail::encode_error(error.what());
                } catch (...) {
                    return detail::encode_error("unknown RPC server failure");
                }
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

            bytes round_trip(const bytes& request) {
                return server_->dispatch(request);
            }

        private:
            router* server_;
        };
    }

    struct rpc_backend : backend_base<rpc_backend> {
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
        static dynamic_value_t to(Context&, const std::string& value) noexcept {
            return dynamic_value_t::borrowed_string(value);
        }

        template <typename Context>
        static dynamic_value_t to(Context&, std::string&& value) {
            return dynamic_value_t::string(std::move(value));
        }

        template <typename Context>
        static optional<std::string> from(Context&, const dynamic_value_t& value) {
            if (value.kind() != rpc::value_kind::string) {
                return optional<std::string>();
            }
            return optional<std::string>(value.copy_string());
        }

        template <typename Context>
        static optional<std::string> from(Context&, dynamic_value_t& value) {
            if (value.kind() != rpc::value_kind::string) {
                return optional<std::string>();
            }
            return optional<std::string>(value.take_string());
        }

        template <typename Context>
        static optional<std::string> from(Context&, dynamic_value_t&& value) {
            if (value.kind() != rpc::value_kind::string) {
                return optional<std::string>();
            }
            return optional<std::string>(value.take_string());
        }
    };
}

#endif
