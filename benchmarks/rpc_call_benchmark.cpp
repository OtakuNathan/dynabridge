#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#if !defined(_WIN32)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/uio.h>
#include <unistd.h>
#endif

#if defined(DYNABRIDGE_HAS_RPCLIB)
#include <rpc/client.h>
#include <rpc/server.h>
#endif

#if defined(DYNABRIDGE_HAS_FLUX_FOUNDRY)
#include "extension/external_async_awaitable.h"
#include "flow/flow.h"
#endif

#if defined(DYNABRIDGE_HAS_GLIB)
#include <gio/gio.h>
#include "executor/gsource_executor.h"
#endif

#define DYNABRIDGE_IMPORT_DEF "tests/rpc_import.def"
#define DYNABRIDGE_EXPORT_DEF "tests/rpc_export.def"
#include "dynabridge/bridge.h"
#undef DYNABRIDGE_EXPORT_DEF
#undef DYNABRIDGE_IMPORT_DEF

#include "dynabridge/backends/rpc.h"

namespace {
    int add(int left, unsigned right) {
        return left + static_cast<int>(right);
    }

    std::string echo(std::string value) {
        return value;
    }

    template <typename Function>
    double measure(std::size_t iterations, Function&& function, volatile int& sink) {
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) {
            sink = function(static_cast<int>(i));
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        return std::chrono::duration<double, std::nano>(elapsed).count()
            / static_cast<double>(iterations);
    }

    template <typename Function>
    double measure_payload(
        std::size_t iterations,
        const std::string& payload,
        Function&& function,
        volatile std::size_t& sink)
    {
        const auto start = std::chrono::steady_clock::now();
        for (std::size_t i = 0; i < iterations; ++i) {
            sink = function(payload).size();
        }
        const auto elapsed = std::chrono::steady_clock::now() - start;
        return std::chrono::duration<double, std::nano>(elapsed).count()
            / static_cast<double>(iterations);
    }

    void print_result(const char* name, double nanoseconds) {
        std::cout << std::left << std::setw(30) << name
                  << std::right << std::fixed << std::setprecision(1)
                  << nanoseconds << " ns/call\n";
    }

#if !defined(_WIN32)
    void write_all(int socket, const void* data, std::size_t size) {
        auto* current = static_cast<const char*>(data);
        while (size != 0) {
            const auto written = ::send(socket, current, size, 0);
            if (written <= 0) {
                throw std::runtime_error("TCP send failed");
            }
            current += written;
            size -= static_cast<std::size_t>(written);
        }
    }

    bool read_all(int socket, void* data, std::size_t size) {
        auto* current = static_cast<char*>(data);
        while (size != 0) {
            const auto received = ::recv(socket, current, size, 0);
            if (received == 0) {
                return false;
            }
            if (received < 0) {
                throw std::runtime_error("TCP receive failed");
            }
            current += received;
            size -= static_cast<std::size_t>(received);
        }
        return true;
    }

    void disable_nagle(int socket) {
        int enabled = 1;
        if (::setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) != 0) {
            throw std::runtime_error("TCP_NODELAY failed");
        }
    }

    void write_frame(int socket, const dynabridge::rpc::bytes& payload) {
        if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
            throw std::runtime_error("TCP frame is too large");
        }
        const auto network_size = htonl(static_cast<std::uint32_t>(payload.size()));
        iovec parts[] = {
            {const_cast<std::uint32_t*>(&network_size), sizeof(network_size)},
            {const_cast<std::uint8_t*>(payload.data()), payload.size()}
        };
        const auto written = ::writev(socket, parts, 2);
        if (written < 0) {
            throw std::runtime_error("TCP writev failed");
        }
        const auto sent = static_cast<std::size_t>(written);
        if (sent < sizeof(network_size)) {
            write_all(socket,
                reinterpret_cast<const char*>(&network_size) + sent,
                sizeof(network_size) - sent);
            write_all(socket, payload.data(), payload.size());
        } else if (sent - sizeof(network_size) < payload.size()) {
            const auto payload_sent = sent - sizeof(network_size);
            write_all(socket, payload.data() + payload_sent, payload.size() - payload_sent);
        }
    }

    class tcp_server {
    public:
        explicit tcp_server(dynabridge::rpc::router& router)
            : router_(&router) {
            listen_ = ::socket(AF_INET, SOCK_STREAM, 0);
            if (listen_ < 0) {
                throw std::runtime_error("TCP socket failed");
            }
            int reuse = 1;
            ::setsockopt(listen_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = 0;
            if (::bind(listen_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
                    || ::listen(listen_, 1) != 0) {
                ::close(listen_);
                throw std::runtime_error("TCP bind/listen failed");
            }
            socklen_t size = sizeof(address);
            ::getsockname(listen_, reinterpret_cast<sockaddr*>(&address), &size);
            port_ = ntohs(address.sin_port);
            worker_ = std::thread([this] { serve(); });
        }

        tcp_server(const tcp_server&) = delete;
        tcp_server& operator=(const tcp_server&) = delete;

        ~tcp_server() {
            ::shutdown(listen_, SHUT_RDWR);
            ::close(listen_);
            if (worker_.joinable()) {
                worker_.join();
            }
        }

        std::uint16_t port() const noexcept { return port_; }

    private:
        void serve() noexcept {
            for (;;) {
                const int client = ::accept(listen_, nullptr, nullptr);
                if (client < 0) {
                    return;
                }
                try {
                    disable_nagle(client);
                    for (;;) {
                        std::uint32_t network_size = 0;
                        if (!read_all(client, &network_size, sizeof(network_size))) {
                            break;
                        }
                        dynabridge::rpc::bytes request(ntohl(network_size));
                        if (!read_all(client, request.data(), request.size())) {
                            break;
                        }
                        auto response = router_->dispatch(request);
                        write_frame(client, response);
                    }
                } catch (...) {
                }
                ::close(client);
            }
        }

        dynabridge::rpc::router* router_;
        int listen_ = -1;
        std::uint16_t port_ = 0;
        std::thread worker_;
    };

    class tcp_transport {
    public:
        explicit tcp_transport(std::uint16_t port) {
            socket_ = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(port);
            if (socket_ < 0
                    || ::connect(socket_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
                if (socket_ >= 0) {
                    ::close(socket_);
                }
                throw std::runtime_error("TCP connect failed");
            }
            disable_nagle(socket_);
        }

        tcp_transport(const tcp_transport&) = delete;
        tcp_transport& operator=(const tcp_transport&) = delete;

        ~tcp_transport() {
            ::shutdown(socket_, SHUT_RDWR);
            ::close(socket_);
        }

        dynabridge::rpc::bytes round_trip(const dynabridge::rpc::bytes& request) {
            write_frame(socket_, request);
            std::uint32_t network_size = 0;
            if (!read_all(socket_, &network_size, sizeof(network_size))) {
                throw std::runtime_error("TCP server closed the connection");
            }
            dynabridge::rpc::bytes response(ntohl(network_size));
            if (!read_all(socket_, response.data(), response.size())) {
                throw std::runtime_error("truncated TCP response");
            }
            return response;
        }

    private:
        int socket_ = -1;
    };

#if defined(DYNABRIDGE_HAS_FLUX_FOUNDRY)
    namespace ff = flux_foundry;
    using flow_error_t = ff::extension::external_async_error_t;

    struct web_io_request {
        tcp_transport* transport;
        dynabridge::rpc::bytes frame;
    };

    struct web_io_state {
        tcp_transport* transport;
        dynabridge::rpc::bytes request;
        dynabridge::rpc::bytes response;
        std::exception_ptr error;
    };

    using web_io_callback_t = ff::extension::external_async_callback_fp_t;
    using web_io_callback_param_t = ff::extension::external_async_callback_param_t;

    void complete_web_io(
        web_io_state* state,
        web_io_callback_t callback,
        web_io_callback_param_t user) noexcept
    {
        try {
            state->response = state->transport->round_trip(state->request);
        } catch (...) {
            state->error = std::current_exception();
        }
        callback(user);
    }

    struct web_io {
        using state_t = web_io_state;

        struct context_t {
            state_t* state;
        };

        using result_t = state_t*;

        static int init_ctx(context_t* ctx, web_io_request* request) noexcept {
            ctx->state = new (std::nothrow) state_t{
                request->transport,
                std::move(request->frame),
                {},
                {}};
            return ctx->state == nullptr ? -1 : 0;
        }

        static void destroy_ctx(context_t* ctx) noexcept {
            delete ctx->state;
            ctx->state = nullptr;
        }

        static void free_result(result_t result) noexcept {
            delete result;
        }

        static int submit(
            context_t* ctx,
            ff::extension::external_async_callback_fp_t callback,
            ff::extension::external_async_callback_param_t user) noexcept
        {
            complete_web_io(ctx->state, callback, user);
            return 0;
        }

        static result_t collect(context_t* ctx) noexcept {
            auto* result = ctx->state;
            ctx->state = nullptr;
            return result;
        }
    };

    using web_io_awaitable_t = ff::extension::external_async_awaitable<web_io>;
    using web_io_result_t = typename web_io_awaitable_t::async_result_type;
    using int_flow_result_t = ff::result_t<int, flow_error_t>;
    using string_flow_result_t = ff::result_t<std::string, flow_error_t>;

    struct string_input {
        const std::string* value;
    };

    template <typename Context>
    auto make_scalar_rpc_flow(Context& ctx) {
        return ff::make_blueprint<int, flow_error_t>()
            | ff::then([&ctx](ff::result_t<int, flow_error_t>&& input)
                -> ff::result_t<web_io_request, flow_error_t> {
                return ff::result_t<web_io_request, flow_error_t>(
                    ff::value_tag,
                    web_io_request{
                        ctx.transport_,
                        dynabridge::rpc::detail::encode_request_values(
                            ctx.method_,
                            dynabridge::to_cast<int>(ctx, std::move(input).value()),
                            dynabridge::to_cast<unsigned>(ctx, 7u))});
            })
            | ff::await_external_async<web_io>()
            | ff::then([&ctx](web_io_result_t&& input) -> int_flow_result_t {
                auto operation = std::move(input).value();
                if (operation->error) {
                    std::rethrow_exception(operation->error);
                }
                return int_flow_result_t(
                    ff::value_tag,
                    dynabridge::from_cast<int>(
                        ctx,
                        dynabridge::rpc::detail::decode_response(operation->response)));
            })
            | ff::end();
    }

    template <typename Context>
    auto make_string_rpc_flow(Context& ctx) {
        return ff::make_blueprint<string_input, flow_error_t>()
            | ff::then([&ctx](ff::result_t<string_input, flow_error_t>&& input)
                -> ff::result_t<web_io_request, flow_error_t> {
                const auto* value = std::move(input).value().value;
                return ff::result_t<web_io_request, flow_error_t>(
                    ff::value_tag,
                    web_io_request{
                        ctx.transport_,
                        dynabridge::rpc::detail::encode_request_values(
                            ctx.method_,
                            dynabridge::to_cast<std::string>(ctx, *value))});
            })
            | ff::await_external_async<web_io>()
            | ff::then([&ctx](web_io_result_t&& input) -> string_flow_result_t {
                auto operation = std::move(input).value();
                if (operation->error) {
                    std::rethrow_exception(operation->error);
                }
                return string_flow_result_t(
                    ff::value_tag,
                    dynabridge::from_cast<std::string>(
                        ctx,
                        dynabridge::rpc::detail::decode_response(operation->response)));
            })
            | ff::end();
    }

    template <typename T>
    struct flow_result_state {
        T value{};
        flow_error_t error{};
        std::atomic<bool> done{false};
    };

    template <typename T>
    struct flow_result_receiver {
        using value_type = ff::result_t<T, flow_error_t>;

        flow_result_state<T>* state;

        void emplace(value_type&& result) noexcept {
            if (result.has_value()) {
                state->value = std::move(result).value();
                state->error = flow_error_t{};
            } else {
                state->error = std::move(result).error();
            }
            state->done.store(true, std::memory_order_release);
        }
    };

    template <typename T, typename Runner, typename Input>
    T run_flow(Runner& runner, flow_result_state<T>& state, Input&& input) {
        state.done.store(false, std::memory_order_relaxed);
        state.error = flow_error_t{};
        runner(std::forward<Input>(input));
        while (!state.done.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        if (state.error) {
            std::rethrow_exception(state.error);
        }
        return std::move(state.value);
    }

#if defined(DYNABRIDGE_HAS_GLIB)
    class glib_runtime {
    public:
        glib_runtime()
            : context_(g_main_context_new(), g_main_context_unref)
        {
            if (!context_) {
                throw std::runtime_error("failed to create GLib RPC runtime");
            }
            if (executor_.register_to(context_.get()) != 0) {
                throw std::runtime_error("failed to register GLib RPC executor");
            }
        }

        glib_runtime(const glib_runtime&) = delete;
        glib_runtime& operator=(const glib_runtime&) = delete;

        ~glib_runtime() = default;

        GMainContext* context() const noexcept {
            return context_.get();
        }

        void iterate() noexcept {
            g_main_context_iteration(context_.get(), TRUE);
        }

        template <typename Task>
        void dispatch(Task&& task) {
            executor_.dispatch(
                ff::task_wrapper_sbo(std::forward<Task>(task)));
        }

    private:
        using context_ptr_t = std::unique_ptr<GMainContext, void (*)(GMainContext*)>;

        context_ptr_t context_;
        ff::gsource_executor<64> executor_;
    };

    template <typename T, typename Runner, typename Input>
    T run_glib_flow(
        glib_runtime& runtime,
        Runner& runner,
        flow_result_state<T>& state,
        Input&& input)
    {
        state.done.store(false, std::memory_order_relaxed);
        state.error = flow_error_t{};
        runner(std::forward<Input>(input));
        while (!state.done.load(std::memory_order_acquire)) {
            runtime.iterate();
        }
        if (state.error) {
            std::rethrow_exception(state.error);
        }
        return std::move(state.value);
    }

    class glib_tcp_transport;

    struct glib_web_io_request {
        glib_tcp_transport* transport;
        dynabridge::rpc::bytes frame;
    };

    struct glib_web_io_state {
        glib_tcp_transport* transport;
        dynabridge::rpc::bytes request;
        dynabridge::rpc::bytes response;
        std::exception_ptr error;
        web_io_callback_t callback;
        web_io_callback_param_t user;
        std::uint32_t request_size;
        std::uint32_t response_size;
        std::size_t request_header_offset;
        std::size_t request_offset;
        std::size_t response_header_offset;
        std::size_t response_offset;
        bool response_initialized;
    };

    class glib_tcp_transport {
    public:
        glib_tcp_transport(GMainContext* context, std::uint16_t port)
            : context_(context) {
            const int socket = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            address.sin_port = htons(port);
            if (socket < 0
                    || ::connect(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
                if (socket >= 0) {
                    ::close(socket);
                }
                throw std::runtime_error("GLib TCP connect failed");
            }
            disable_nagle(socket);

            GError* error = nullptr;
            socket_ = g_socket_new_from_fd(socket, &error);
            if (socket_ == nullptr) {
                const std::string message = error == nullptr
                    ? "g_socket_new_from_fd failed"
                    : error->message;
                g_clear_error(&error);
                ::close(socket);
                throw std::runtime_error(message);
            }
            g_socket_set_blocking(socket_, FALSE);

            read_source_ = g_socket_create_source(
                socket_, static_cast<GIOCondition>(G_IO_IN | G_IO_ERR | G_IO_HUP), nullptr);
            if (read_source_ == nullptr) {
                close_socket();
                throw std::runtime_error("failed to create persistent GLib TCP read source");
            }
            g_source_set_callback(read_source_, G_SOURCE_FUNC(on_read_ready), this, nullptr);
            if (g_source_attach(read_source_, context_) == 0) {
                g_source_unref(read_source_);
                read_source_ = nullptr;
                close_socket();
                throw std::runtime_error("failed to attach persistent GLib TCP read source");
            }
        }

        glib_tcp_transport(const glib_tcp_transport&) = delete;
        glib_tcp_transport& operator=(const glib_tcp_transport&) = delete;

        ~glib_tcp_transport() {
            if (read_source_ != nullptr) {
                g_source_destroy(read_source_);
                g_source_unref(read_source_);
            }
            close_socket();
        }

        int submit(
            glib_web_io_state* state,
            web_io_callback_t callback,
            web_io_callback_param_t user) noexcept
        {
            state->callback = callback;
            state->user = user;
            state->request_size = htonl(static_cast<std::uint32_t>(state->request.size()));
            glib_web_io_state* expected = nullptr;
            if (!active_.compare_exchange_strong(
                    expected, state, std::memory_order_release, std::memory_order_relaxed)) {
                return -1;
            }
            try {
                if (!write_request(socket_, state) && !attach(state, G_IO_OUT, on_write)) {
                    throw std::runtime_error("failed to attach GLib TCP write source");
                }
                return 0;
            } catch (...) {
                state->error = std::current_exception();
                complete(state);
                return 0;
            }
        }

    private:
        void close_socket() noexcept {
            if (socket_ != nullptr) {
                g_socket_close(socket_, nullptr);
                g_object_unref(socket_);
                socket_ = nullptr;
            }
        }

        static bool is_would_block(GError* error) noexcept {
            return error != nullptr
                && g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK);
        }

        static void throw_socket_error(const char* operation, GError* error) {
            const std::string detail = error == nullptr ? "unknown error" : error->message;
            g_clear_error(&error);
            throw std::runtime_error(std::string(operation) + ": " + detail);
        }

        static bool send_part(
            GSocket* socket,
            const void* data,
            std::size_t size,
            std::size_t& offset)
        {
            const auto* bytes = static_cast<const char*>(data);
            while (offset < size) {
                GError* error = nullptr;
                const auto sent = g_socket_send(
                    socket,
                    bytes + offset,
                    static_cast<gsize>(size - offset),
                    nullptr,
                    &error);
                if (sent > 0) {
                    offset += static_cast<std::size_t>(sent);
                    continue;
                }
                if (sent < 0 && is_would_block(error)) {
                    g_clear_error(&error);
                    return false;
                }
                throw_socket_error("GLib TCP send failed", error);
            }
            return true;
        }

        static bool receive_part(
            GSocket* socket,
            void* data,
            std::size_t size,
            std::size_t& offset)
        {
            auto* bytes = static_cast<char*>(data);
            while (offset < size) {
                GError* error = nullptr;
                const auto received = g_socket_receive(
                    socket,
                    bytes + offset,
                    static_cast<gsize>(size - offset),
                    nullptr,
                    &error);
                if (received > 0) {
                    offset += static_cast<std::size_t>(received);
                    continue;
                }
                if (received < 0 && is_would_block(error)) {
                    g_clear_error(&error);
                    return false;
                }
                if (received == 0) {
                    g_clear_error(&error);
                    throw std::runtime_error("GLib TCP peer closed the connection");
                }
                throw_socket_error("GLib TCP receive failed", error);
            }
            return true;
        }

        static bool write_request(GSocket* socket, glib_web_io_state* state) {
            return send_part(socket, &state->request_size,
                       sizeof(state->request_size), state->request_header_offset)
                && send_part(socket, state->request.data(),
                    state->request.size(), state->request_offset);
        }

        void complete(glib_web_io_state* state) noexcept {
            const auto callback = state->callback;
            const auto user = state->user;
            active_.store(nullptr, std::memory_order_release);
            callback(user);
        }

        static gboolean fail(glib_web_io_state* state) noexcept {
            state->error = std::current_exception();
            state->transport->complete(state);
            return G_SOURCE_REMOVE;
        }

        static gboolean on_write(
            GSocket* socket,
            GIOCondition condition,
            gpointer user) noexcept
        {
            auto* state = static_cast<glib_web_io_state*>(user);
            try {
                if ((condition & (G_IO_ERR | G_IO_HUP | G_IO_NVAL)) != 0) {
                    throw std::runtime_error("GLib TCP socket failed while writing");
                }
                if (!write_request(socket, state)) {
                    return G_SOURCE_CONTINUE;
                }
                return G_SOURCE_REMOVE;
            } catch (...) {
                return fail(state);
            }
        }

        static gboolean on_read_ready(
            GSocket* socket,
            GIOCondition condition,
            gpointer user) noexcept
        {
            auto* transport = static_cast<glib_tcp_transport*>(user);
            auto* state = transport->active_.load(std::memory_order_acquire);
            if (state == nullptr) {
                return G_SOURCE_CONTINUE;
            }
            try {
                if ((condition & (G_IO_ERR | G_IO_NVAL)) != 0) {
                    throw std::runtime_error("GLib TCP socket failed while reading");
                }
                if (!receive_part(socket, &state->response_size,
                        sizeof(state->response_size), state->response_header_offset)) {
                    return G_SOURCE_CONTINUE;
                }
                if (!state->response_initialized) {
                    state->response.resize(ntohl(state->response_size));
                    state->response_initialized = true;
                }
                if (!receive_part(socket, state->response.data(),
                        state->response.size(), state->response_offset)) {
                    return G_SOURCE_CONTINUE;
                }
                transport->complete(state);
                return G_SOURCE_CONTINUE;
            } catch (...) {
                state->error = std::current_exception();
                transport->complete(state);
                return G_SOURCE_CONTINUE;
            }
        }

        bool attach(
            glib_web_io_state* state,
            GIOCondition condition,
            GSocketSourceFunc callback) noexcept
        {
            auto* source = g_socket_create_source(socket_, condition, nullptr);
            if (source == nullptr) {
                return false;
            }
            g_source_set_callback(source, G_SOURCE_FUNC(callback), state, nullptr);
            const auto id = g_source_attach(source, context_);
            g_source_unref(source);
            return id != 0;
        }

        GMainContext* context_;
        GSocket* socket_ = nullptr;
        GSource* read_source_ = nullptr;
        std::atomic<glib_web_io_state*> active_{nullptr};
    };

    struct glib_web_io {
        using state_t = glib_web_io_state;
        using result_t = state_t*;

        struct context_t {
            state_t* state;
        };

        static int init_ctx(context_t* ctx, glib_web_io_request* request) noexcept {
            if (request->frame.size() > std::numeric_limits<std::uint32_t>::max()) {
                return -1;
            }
            ctx->state = new (std::nothrow) state_t{
                request->transport,
                std::move(request->frame),
                {},
                {},
                nullptr,
                nullptr,
                0,
                0,
                0,
                0,
                0,
                0,
                false};
            return ctx->state == nullptr ? -1 : 0;
        }

        static void destroy_ctx(context_t* ctx) noexcept {
            delete ctx->state;
            ctx->state = nullptr;
        }

        static void free_result(result_t result) noexcept {
            delete result;
        }

        static int submit(
            context_t* ctx,
            web_io_callback_t callback,
            web_io_callback_param_t user) noexcept
        {
            return ctx->state->transport->submit(ctx->state, callback, user);
        }

        static result_t collect(context_t* ctx) noexcept {
            auto* result = ctx->state;
            ctx->state = nullptr;
            return result;
        }
    };

    using glib_web_io_awaitable_t = ff::extension::external_async_awaitable<glib_web_io>;
    using glib_web_io_result_t = typename glib_web_io_awaitable_t::async_result_type;

    template <typename Context>
    auto make_glib_scalar_rpc_flow(Context& ctx) {
        return ff::make_blueprint<int, flow_error_t>()
            | ff::then([&ctx](ff::result_t<int, flow_error_t>&& input)
                -> ff::result_t<glib_web_io_request, flow_error_t> {
                return ff::result_t<glib_web_io_request, flow_error_t>(
                    ff::value_tag,
                    glib_web_io_request{
                        ctx.transport_,
                        dynabridge::rpc::detail::encode_request_values(
                            ctx.method_,
                            dynabridge::to_cast<int>(ctx, std::move(input).value()),
                            dynabridge::to_cast<unsigned>(ctx, 7u))});
            })
            | ff::await_external_async<glib_web_io>()
            | ff::then([&ctx](glib_web_io_result_t&& input) -> int_flow_result_t {
                auto operation = std::move(input).value();
                if (operation->error) {
                    std::rethrow_exception(operation->error);
                }
                return int_flow_result_t(
                    ff::value_tag,
                    dynabridge::from_cast<int>(
                        ctx,
                        dynabridge::rpc::detail::decode_response(operation->response)));
            })
            | ff::end();
    }

    template <typename Context>
    auto make_glib_string_rpc_flow(Context& ctx) {
        return ff::make_blueprint<string_input, flow_error_t>()
            | ff::then([&ctx](ff::result_t<string_input, flow_error_t>&& input)
                -> ff::result_t<glib_web_io_request, flow_error_t> {
                const auto* value = std::move(input).value().value;
                return ff::result_t<glib_web_io_request, flow_error_t>(
                    ff::value_tag,
                    glib_web_io_request{
                        ctx.transport_,
                        dynabridge::rpc::detail::encode_request_values(
                            ctx.method_,
                            dynabridge::to_cast<std::string>(ctx, *value))});
            })
            | ff::await_external_async<glib_web_io>()
            | ff::then([&ctx](glib_web_io_result_t&& input) -> string_flow_result_t {
                auto operation = std::move(input).value();
                if (operation->error) {
                    std::rethrow_exception(operation->error);
                }
                return string_flow_result_t(
                    ff::value_tag,
                    dynabridge::from_cast<std::string>(
                        ctx,
                        dynabridge::rpc::detail::decode_response(operation->response)));
            })
            | ff::end();
    }
#endif
#endif

#if defined(DYNABRIDGE_HAS_RPCLIB)
    std::uint16_t unused_loopback_port() {
        const int socket = ::socket(AF_INET, SOCK_STREAM, 0);
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        address.sin_port = 0;
        if (socket < 0
                || ::bind(socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
            throw std::runtime_error("could not reserve benchmark port");
        }
        socklen_t size = sizeof(address);
        ::getsockname(socket, reinterpret_cast<sockaddr*>(&address), &size);
        ::close(socket);
        return ntohs(address.sin_port);
    }
#endif
#endif
}

int main(int argc, char** argv) {
    const std::size_t requested_iterations = argc > 1
        ? static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10))
        : 100000;
    const std::size_t local_iterations = requested_iterations == 0 ? 1 : requested_iterations;
    const std::size_t tcp_iterations = local_iterations / 10 == 0
        ? 1 : local_iterations / 10;

    dynabridge::rpc_backend::export_context_t export_ctx;
    dynabridge::rpc::router module;
    dynabridge::export_rpc_add(export_ctx, module, &add);
    dynabridge::export_rpc_echo(export_ctx, module, &echo);
    dynabridge::rpc::loopback_transport loopback(module);
    using loopback_context_t = dynabridge::rpc_backend::import_context_t<
        dynabridge::rpc::loopback_transport>;
    auto loopback_ctx = dynabridge::import_from<
        dynabridge::import_symbols::rpc_add, loopback_context_t>(loopback);

    volatile int sink = 0;
    volatile std::size_t payload_sink = 0;
    print_result("direct C++", measure(local_iterations,
        [](int value) { return add(value, 7u); }, sink));
    print_result("dynabridge framed loopback", measure(local_iterations,
        [&loopback_ctx](int value) { return dynabridge::call_rpc_add(loopback_ctx, value, 7u); }, sink));

    auto loopback_echo_ctx = dynabridge::import_from<
        dynabridge::import_symbols::rpc_echo, loopback_context_t>(loopback);
    const std::string payload_16(16, 'x');
    const std::string payload_1k(1024, 'x');
    const std::string payload_64k(64 * 1024, 'x');
    if (dynabridge::call_rpc_echo(loopback_echo_ctx, payload_16) != payload_16
            || dynabridge::call_rpc_echo(loopback_echo_ctx, payload_1k) != payload_1k
            || dynabridge::call_rpc_echo(loopback_echo_ctx, payload_64k) != payload_64k) {
        throw std::runtime_error("dynabridge loopback payload verification failed");
    }
    print_result("dynabridge loopback 16 B", measure_payload(local_iterations,
        payload_16,
        [&loopback_echo_ctx](const std::string& value) {
            return dynabridge::call_rpc_echo(loopback_echo_ctx, value);
        }, payload_sink));
    print_result("dynabridge loopback 1 KiB", measure_payload(
        local_iterations / 2 == 0 ? 1 : local_iterations / 2,
        payload_1k,
        [&loopback_echo_ctx](const std::string& value) {
            return dynabridge::call_rpc_echo(loopback_echo_ctx, value);
        }, payload_sink));
    print_result("dynabridge loopback 64 KiB", measure_payload(
        local_iterations / 100 == 0 ? 1 : local_iterations / 100,
        payload_64k,
        [&loopback_echo_ctx](const std::string& value) {
            return dynabridge::call_rpc_echo(loopback_echo_ctx, value);
        }, payload_sink));

#if !defined(_WIN32)
    tcp_server server(module);
    {
        tcp_transport transport(server.port());
        using tcp_context_t = dynabridge::rpc_backend::import_context_t<tcp_transport>;
        auto tcp_ctx = dynabridge::import_from<
            dynabridge::import_symbols::rpc_add, tcp_context_t>(transport);
        auto tcp_echo_ctx = dynabridge::import_from<
            dynabridge::import_symbols::rpc_echo, tcp_context_t>(transport);
        if (dynabridge::call_rpc_echo(tcp_echo_ctx, payload_16) != payload_16
                || dynabridge::call_rpc_echo(tcp_echo_ctx, payload_1k) != payload_1k
                || dynabridge::call_rpc_echo(tcp_echo_ctx, payload_64k) != payload_64k) {
            throw std::runtime_error("dynabridge TCP payload verification failed");
        }
        print_result("dynabridge TCP loopback", measure(tcp_iterations,
            [&tcp_ctx](int value) { return dynabridge::call_rpc_add(tcp_ctx, value, 7u); }, sink));
        print_result("dynabridge TCP 16 B", measure_payload(tcp_iterations,
            payload_16,
            [&tcp_echo_ctx](const std::string& value) {
                return dynabridge::call_rpc_echo(tcp_echo_ctx, value);
            }, payload_sink));
        print_result("dynabridge TCP 1 KiB", measure_payload(
            tcp_iterations / 2 == 0 ? 1 : tcp_iterations / 2,
            payload_1k,
            [&tcp_echo_ctx](const std::string& value) {
                return dynabridge::call_rpc_echo(tcp_echo_ctx, value);
            }, payload_sink));
        print_result("dynabridge TCP 64 KiB", measure_payload(
            tcp_iterations / 20 == 0 ? 1 : tcp_iterations / 20,
            payload_64k,
            [&tcp_echo_ctx](const std::string& value) {
                return dynabridge::call_rpc_echo(tcp_echo_ctx, value);
            }, payload_sink));

#if defined(DYNABRIDGE_HAS_FLUX_FOUNDRY)
        auto scalar_flow = make_scalar_rpc_flow(tcp_ctx);
        auto string_flow = make_string_rpc_flow(tcp_echo_ctx);

        flow_result_state<int> scalar_state;
        flow_result_state<std::string> string_state;
        auto scalar_runner = ff::make_fast_runner(
            std::move(scalar_flow), flow_result_receiver<int>{&scalar_state});
        auto string_runner = ff::make_fast_runner(
            std::move(string_flow), flow_result_receiver<std::string>{&string_state});

        if (run_flow(string_runner, string_state, string_input{&payload_16}) != payload_16
                || run_flow(string_runner, string_state, string_input{&payload_1k}) != payload_1k
                || run_flow(string_runner, string_state, string_input{&payload_64k}) != payload_64k) {
            throw std::runtime_error("dynabridge + Flux Foundry payload verification failed");
        }
        print_result("dynabridge + FF TCP", measure(tcp_iterations,
            [&scalar_runner, &scalar_state](int value) {
                return run_flow(scalar_runner, scalar_state, value);
            }, sink));
        print_result("dynabridge + FF TCP 16 B", measure_payload(tcp_iterations,
            payload_16,
            [&string_runner, &string_state](const std::string& value) {
                return run_flow(string_runner, string_state, string_input{&value});
            }, payload_sink));
        print_result("dynabridge + FF TCP 1 KiB", measure_payload(
            tcp_iterations / 2 == 0 ? 1 : tcp_iterations / 2,
            payload_1k,
            [&string_runner, &string_state](const std::string& value) {
                return run_flow(string_runner, string_state, string_input{&value});
            }, payload_sink));
        print_result("dynabridge + FF TCP 64 KiB", measure_payload(
            tcp_iterations / 20 == 0 ? 1 : tcp_iterations / 20,
            payload_64k,
            [&string_runner, &string_state](const std::string& value) {
                return run_flow(string_runner, string_state, string_input{&value});
            }, payload_sink));
#else
        std::cout << "dynabridge + FF TCP           not built (Flux Foundry not found)\n";
#endif
    }

#if defined(DYNABRIDGE_HAS_GLIB)
    {
        glib_runtime runtime;
        bool ingress_dispatched = false;
        runtime.dispatch([&ingress_dispatched]() noexcept {
            ingress_dispatched = true;
        });
        while (!ingress_dispatched) {
            runtime.iterate();
        }
        glib_tcp_transport transport(runtime.context(), server.port());
        using glib_context_t = dynabridge::rpc_backend::import_context_t<glib_tcp_transport>;
        auto glib_ctx = dynabridge::import_from<
            dynabridge::import_symbols::rpc_add, glib_context_t>(transport);
        auto glib_echo_ctx = dynabridge::import_from<
            dynabridge::import_symbols::rpc_echo, glib_context_t>(transport);
        auto scalar_flow = make_glib_scalar_rpc_flow(glib_ctx);
        auto string_flow = make_glib_string_rpc_flow(glib_echo_ctx);
        flow_result_state<int> scalar_state;
        flow_result_state<std::string> string_state;
        auto scalar_runner = ff::make_fast_runner(
            std::move(scalar_flow), flow_result_receiver<int>{&scalar_state});
        auto string_runner = ff::make_fast_runner(
            std::move(string_flow), flow_result_receiver<std::string>{&string_state});

        if (run_glib_flow(runtime, string_runner, string_state,
                    string_input{&payload_16}) != payload_16
                || run_glib_flow(runtime, string_runner, string_state,
                    string_input{&payload_1k}) != payload_1k
                || run_glib_flow(runtime, string_runner, string_state,
                    string_input{&payload_64k}) != payload_64k) {
            throw std::runtime_error("dynabridge + Flux Foundry GLib payload verification failed");
        }
        print_result("dynabridge + FF GLib TCP", measure(tcp_iterations,
            [&runtime, &scalar_runner, &scalar_state](int value) {
                return run_glib_flow(runtime, scalar_runner, scalar_state, value);
            }, sink));
        print_result("dynabridge + FF GLib 16 B", measure_payload(tcp_iterations,
            payload_16,
            [&runtime, &string_runner, &string_state](const std::string& value) {
                return run_glib_flow(
                    runtime, string_runner, string_state, string_input{&value});
            }, payload_sink));
        print_result("dynabridge + FF GLib 1 KiB", measure_payload(
            tcp_iterations / 2 == 0 ? 1 : tcp_iterations / 2,
            payload_1k,
            [&runtime, &string_runner, &string_state](const std::string& value) {
                return run_glib_flow(
                    runtime, string_runner, string_state, string_input{&value});
            }, payload_sink));
        print_result("dynabridge + FF GLib 64 KiB", measure_payload(
            tcp_iterations / 20 == 0 ? 1 : tcp_iterations / 20,
            payload_64k,
            [&runtime, &string_runner, &string_state](const std::string& value) {
                return run_glib_flow(
                    runtime, string_runner, string_state, string_input{&value});
            }, payload_sink));
    }
#else
    std::cout << "dynabridge + FF GLib          not built (GLib/GIO not found)\n";
#endif

#if defined(DYNABRIDGE_HAS_RPCLIB)
    const auto rpc_port = unused_loopback_port();
    rpc::server rpc_server(rpc_port);
    rpc_server.bind("rpc_add", &add);
    rpc_server.bind("rpc_echo", &echo);
    rpc_server.async_run(1);
    rpc::client rpc_client("127.0.0.1", rpc_port);
    if (rpc_client.call("rpc_echo", payload_16).as<std::string>() != payload_16
            || rpc_client.call("rpc_echo", payload_1k).as<std::string>() != payload_1k
            || rpc_client.call("rpc_echo", payload_64k).as<std::string>() != payload_64k) {
        throw std::runtime_error("rpclib TCP payload verification failed");
    }
    print_result("rpclib TCP loopback", measure(tcp_iterations,
        [&rpc_client](int value) {
            return rpc_client.call("rpc_add", value, 7u).as<int>();
        }, sink));
    print_result("rpclib TCP 16 B", measure_payload(tcp_iterations,
        payload_16,
        [&rpc_client](const std::string& value) {
            return rpc_client.call("rpc_echo", value).as<std::string>();
        }, payload_sink));
    print_result("rpclib TCP 1 KiB", measure_payload(
        tcp_iterations / 2 == 0 ? 1 : tcp_iterations / 2,
        payload_1k,
        [&rpc_client](const std::string& value) {
            return rpc_client.call("rpc_echo", value).as<std::string>();
        }, payload_sink));
    print_result("rpclib TCP 64 KiB", measure_payload(
        tcp_iterations / 20 == 0 ? 1 : tcp_iterations / 20,
        payload_64k,
        [&rpc_client](const std::string& value) {
            return rpc_client.call("rpc_echo", value).as<std::string>();
        }, payload_sink));
    rpc_server.stop();
#else
    std::cout << "rpclib TCP loopback           not built (rpclib not found)\n";
#endif
#else
    std::cout << "TCP comparison                unavailable on this benchmark build\n";
#endif

    std::cout << "iterations: local=" << local_iterations
              << ", tcp=" << tcp_iterations << ", sink=" << sink
              << ", payload_sink=" << payload_sink << '\n';
    return 0;
}
