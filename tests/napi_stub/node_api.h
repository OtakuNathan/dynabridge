#ifndef SRC_NODE_API_H_
#define SRC_NODE_API_H_

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#define NAPI_AUTO_LENGTH static_cast<std::size_t>(-1)

typedef enum napi_status {
    napi_ok = 0,
    napi_generic_failure = 1
} napi_status;

typedef enum napi_valuetype {
    napi_undefined = 0,
    napi_null = 1,
    napi_boolean = 2,
    napi_number = 3,
    napi_string = 4,
    napi_symbol = 5,
    napi_object = 6,
    napi_function = 7,
    napi_external = 8,
    napi_bigint = 9
} napi_valuetype;

struct napi_env__;
struct napi_value__;
struct napi_ref__;
struct napi_callback_info__;

typedef napi_env__* napi_env;
typedef napi_value__* napi_value;
typedef napi_ref__* napi_ref;
typedef napi_callback_info__* napi_callback_info;
typedef napi_value (*napi_callback)(napi_env env, napi_callback_info info);
typedef void (*napi_finalize)(napi_env env, void* finalize_data, void* finalize_hint);

struct napi_value__ {
    enum class kind_t {
        undefined,
        int32,
        object,
        function,
        external
    };

    kind_t kind = kind_t::undefined;
    std::int64_t number = 0;
    std::map<std::string, napi_value> properties;
    napi_callback callback = nullptr;
    void* data = nullptr;
    napi_value prototype = nullptr;
    void* native = nullptr;
    void* external = nullptr;
    napi_finalize finalizer = nullptr;
    void* finalizer_hint = nullptr;
};

struct napi_ref__ {
    napi_value value = nullptr;
};

struct napi_env__ {
    std::vector<std::unique_ptr<napi_value__>> values;
    std::vector<std::unique_ptr<napi_ref__>> refs;
    napi_value global = nullptr;
    napi_value undefined = nullptr;
};

struct napi_callback_info__ {
    std::vector<napi_value> args;
    napi_value this_arg = nullptr;
    void* data = nullptr;
};

inline napi_value napi_stub_make_value(napi_env env, napi_value__::kind_t kind) {
    std::unique_ptr<napi_value__> value(new napi_value__);
    value->kind = kind;
    napi_value raw = value.get();
    env->values.push_back(std::move(value));
    return raw;
}

inline napi_env napi_stub_create_env() {
    napi_env env = new napi_env__;
    env->undefined = napi_stub_make_value(env, napi_value__::kind_t::undefined);
    env->global = napi_stub_make_value(env, napi_value__::kind_t::object);
    return env;
}

inline void napi_stub_delete_env(napi_env env) {
    for (auto& value : env->values) {
        if (value->finalizer != nullptr && value->native != nullptr) {
            value->finalizer(env, value->native, value->finalizer_hint);
            value->native = nullptr;
            value->finalizer = nullptr;
            value->finalizer_hint = nullptr;
        }
    }
    delete env;
}

inline napi_status napi_get_undefined(napi_env env, napi_value* result) {
    *result = env->undefined;
    return napi_ok;
}

inline napi_status napi_get_global(napi_env env, napi_value* result) {
    *result = env->global;
    return napi_ok;
}

inline napi_status napi_create_object(napi_env env, napi_value* result) {
    *result = napi_stub_make_value(env, napi_value__::kind_t::object);
    return napi_ok;
}

inline napi_status napi_create_int32(napi_env env, int value, napi_value* result) {
    *result = napi_stub_make_value(env, napi_value__::kind_t::int32);
    (*result)->number = value;
    return napi_ok;
}

inline napi_status napi_create_uint32(napi_env env, unsigned value, napi_value* result) {
    *result = napi_stub_make_value(env, napi_value__::kind_t::int32);
    (*result)->number = value;
    return napi_ok;
}

inline napi_status napi_get_value_int32(napi_env, napi_value value, int* result) {
    if (value == nullptr || value->kind != napi_value__::kind_t::int32) {
        return napi_generic_failure;
    }
    *result = static_cast<int>(value->number);
    return napi_ok;
}

inline napi_status napi_get_value_int64(napi_env, napi_value value, std::int64_t* result) {
    if (value == nullptr || value->kind != napi_value__::kind_t::int32) {
        return napi_generic_failure;
    }
    *result = value->number;
    return napi_ok;
}

inline napi_status napi_get_value_uint32(napi_env, napi_value value, unsigned* result) {
    if (value == nullptr || value->kind != napi_value__::kind_t::int32) {
        return napi_generic_failure;
    }
    *result = static_cast<unsigned>(value->number);
    return napi_ok;
}

inline napi_status napi_typeof(napi_env, napi_value value, napi_valuetype* result) {
    if (value == nullptr) {
        return napi_generic_failure;
    }

    switch (value->kind) {
    case napi_value__::kind_t::undefined:
        *result = napi_undefined;
        break;
    case napi_value__::kind_t::int32:
        *result = napi_number;
        break;
    case napi_value__::kind_t::object:
        *result = napi_object;
        break;
    case napi_value__::kind_t::function:
        *result = napi_function;
        break;
    case napi_value__::kind_t::external:
        *result = napi_external;
        break;
    }
    return napi_ok;
}

inline napi_status napi_create_external(
    napi_env env,
    void* data,
    napi_finalize finalize_cb,
    void* finalize_hint,
    napi_value* result)
{
    *result = napi_stub_make_value(env, napi_value__::kind_t::external);
    (*result)->external = data;
    (*result)->finalizer = finalize_cb;
    (*result)->finalizer_hint = finalize_hint;
    return napi_ok;
}

inline napi_status napi_get_value_external(napi_env, napi_value value, void** result) {
    if (value == nullptr || value->kind != napi_value__::kind_t::external) {
        return napi_generic_failure;
    }
    *result = value->external;
    return napi_ok;
}

inline napi_status napi_create_reference(napi_env env, napi_value value, std::uint32_t, napi_ref* result) {
    std::unique_ptr<napi_ref__> ref(new napi_ref__);
    ref->value = value;
    *result = ref.get();
    env->refs.push_back(std::move(ref));
    return napi_ok;
}

inline napi_status napi_get_reference_value(napi_env, napi_ref ref, napi_value* result) {
    *result = ref->value;
    return napi_ok;
}

inline napi_status napi_delete_reference(napi_env, napi_ref ref) {
    ref->value = nullptr;
    return napi_ok;
}

inline napi_status napi_wrap(
    napi_env env,
    napi_value object,
    void* native,
    napi_finalize finalize_cb,
    void* finalize_hint,
    napi_ref* result)
{
    object->native = native;
    object->finalizer = finalize_cb;
    object->finalizer_hint = finalize_hint;
    if (result != nullptr) {
        return napi_create_reference(env, object, 1, result);
    }
    return napi_ok;
}

inline napi_status napi_unwrap(napi_env, napi_value object, void** result) {
    if (object == nullptr || object->native == nullptr) {
        return napi_generic_failure;
    }
    *result = object->native;
    return napi_ok;
}

inline napi_status napi_add_finalizer(
    napi_env env,
    napi_value object,
    void* finalize_data,
    napi_finalize finalize_cb,
    void* finalize_hint,
    napi_ref* result)
{
    object->native = finalize_data;
    object->finalizer = finalize_cb;
    object->finalizer_hint = finalize_hint;
    if (result != nullptr) {
        return napi_create_reference(env, object, 1, result);
    }
    return napi_ok;
}

inline napi_status napi_throw_error(napi_env, const char*, const char*) {
    return napi_ok;
}

inline napi_status napi_create_function(
    napi_env env,
    const char*,
    std::size_t,
    napi_callback callback,
    void* data,
    napi_value* result)
{
    *result = napi_stub_make_value(env, napi_value__::kind_t::function);
    (*result)->callback = callback;
    (*result)->data = data;
    return napi_ok;
}

inline napi_status napi_get_cb_info(
    napi_env,
    napi_callback_info info,
    std::size_t* argc,
    napi_value* argv,
    napi_value* this_arg,
    void** data)
{
    if (argc != nullptr) {
        const std::size_t available = info->args.size();
        const std::size_t requested = *argc;
        if (argv != nullptr) {
            const std::size_t count = requested < available ? requested : available;
            for (std::size_t i = 0; i < count; ++i) {
                argv[i] = info->args[i];
            }
        }
        *argc = available;
    }

    if (this_arg != nullptr) {
        *this_arg = info->this_arg;
    }

    if (data != nullptr) {
        *data = info->data;
    }

    return napi_ok;
}

inline napi_status napi_call_function(
    napi_env env,
    napi_value receiver,
    napi_value function,
    std::size_t argc,
    const napi_value* argv,
    napi_value* result)
{
    napi_callback_info__ info;
    info.this_arg = receiver;
    info.data = function->data;
    for (std::size_t i = 0; i < argc; ++i) {
        info.args.push_back(argv[i]);
    }

    *result = function->callback(env, &info);
    return *result == nullptr ? napi_generic_failure : napi_ok;
}

inline napi_status napi_new_instance(
    napi_env env,
    napi_value constructor,
    std::size_t argc,
    const napi_value* argv,
    napi_value* result)
{
    napi_value self = nullptr;
    napi_create_object(env, &self);

    napi_callback_info__ info;
    info.this_arg = self;
    info.data = constructor->data;
    for (std::size_t i = 0; i < argc; ++i) {
        info.args.push_back(argv[i]);
    }

    napi_value returned = constructor->callback(env, &info);
    *result = returned == nullptr ? self : returned;
    return napi_ok;
}

inline napi_status napi_set_named_property(
    napi_env,
    napi_value object,
    const char* name,
    napi_value value)
{
    object->properties[name] = value;
    return napi_ok;
}

inline napi_status napi_get_named_property(
    napi_env,
    napi_value object,
    const char* name,
    napi_value* result)
{
    auto iter = object->properties.find(name);
    if (iter == object->properties.end()) {
        return napi_generic_failure;
    }
    *result = iter->second;
    return napi_ok;
}

inline napi_status napi_define_class(
    napi_env env,
    const char*,
    std::size_t,
    napi_callback callback,
    void* data,
    std::size_t,
    const void*,
    napi_value* result)
{
    napi_create_function(env, nullptr, NAPI_AUTO_LENGTH, callback, data, result);
    napi_value prototype = nullptr;
    napi_create_object(env, &prototype);
    (*result)->prototype = prototype;
    (*result)->properties["prototype"] = prototype;
    return napi_ok;
}

#endif //SRC_NODE_API_H_
