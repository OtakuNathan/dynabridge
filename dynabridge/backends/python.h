#ifndef DYNABRIDGE_BACKENDS_PYTHON_H
#define DYNABRIDGE_BACKENDS_PYTHON_H

#include "python_api.h"

#include <cstddef>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "../backend_base.h"

namespace dynabridge {
    struct py_backend : backend_base<py_backend> {
        using dynamic_value_t = PyObject*;

        enum class ref_policy { borrowed, owned };

        class object_ref {
        public:
            object_ref() noexcept = default;

            object_ref(PyObject* object, ref_policy policy = ref_policy::borrowed) noexcept
                : object_(object) {
                if (object_ != nullptr && policy == ref_policy::borrowed) {
                    Py_INCREF(object_);
                }
            }

            object_ref(const object_ref&) = delete;
            object_ref& operator=(const object_ref&) = delete;

            object_ref(object_ref&& other) noexcept : object_(other.release()) {}

            object_ref& operator=(object_ref&& other) noexcept {
                if (this != &other) {
                    reset(other.release(), ref_policy::owned);
                }
                return *this;
            }

            ~object_ref() noexcept { Py_XDECREF(object_); }

            PyObject* get() const noexcept { return object_; }

            PyObject* release() noexcept {
                PyObject* const object = object_;
                object_ = nullptr;
                return object;
            }

            void reset(PyObject* object = nullptr, ref_policy policy = ref_policy::borrowed) noexcept {
                Py_XDECREF(object_);
                object_ = object;
                if (object_ != nullptr && policy == ref_policy::borrowed) {
                    Py_INCREF(object_);
                }
            }

            explicit operator bool() const noexcept { return object_ != nullptr; }

        private:
            PyObject* object_ = nullptr;
        };

        template <typename Receiver, typename Direction = typename bridge_direction<Receiver>::type>
        class object_t;

        class module_t {
        public:
            module_t() noexcept = default;

            explicit module_t(PyObject* module, ref_policy policy = ref_policy::borrowed) noexcept
                : module_(module, policy) {}

            PyObject* get() const noexcept { return module_.get(); }

            void define(const char* name, PyObject* value) {
                if (PyModule_AddObject(get(), name, value) != 0) {
                    Py_XDECREF(value);
                    throw std::runtime_error("PyModule_AddObject failed");
                }
            }

        private:
            object_ref module_;
        };

        class class_target_t {
        public:
            class_target_t() noexcept = default;

            explicit class_target_t(PyObject* type, ref_policy policy = ref_policy::borrowed) noexcept
                : type_(type, policy) {}

            PyObject* get() const noexcept { return type_.get(); }

            void define(const char* name, PyObject* value) {
                if (PyObject_SetAttrString(get(), name, value) != 0) {
                    Py_XDECREF(value);
                    throw std::runtime_error("PyObject_SetAttrString failed");
                }
                Py_DECREF(value);
            }

        private:
            object_ref type_;
        };

        class context_t {
        public:
            using backend_t = py_backend;

            context_t() noexcept = default;

            explicit context_t(PyObject* callable, ref_policy policy = ref_policy::borrowed) noexcept
                : callable_(callable, policy) {}

            PyObject* callable() const noexcept { return callable_.get(); }

            void set_callable(PyObject* callable, ref_policy policy = ref_policy::borrowed) noexcept {
                callable_.reset(callable, policy);
            }

        private:
            object_ref callable_;
        };

        using import_context_t = context_t;

        class export_context_t : public context_t {
        public:
            using context_t::context_t;

            template <typename Class>
            void store_export_class(class_target_t target) {
                classes_.template store<Class>(std::move(target));
            }

            template <typename Class>
            class_target_t& export_class() {
                return classes_.template get<Class>();
            }

        private:
            export_class_registry<class_target_t> classes_;
        };

        template <typename T>
        struct converter;

        template <typename Receiver, typename Direction>
        class object_t
            : public object_ref,
              public object_base_selector<
                  object_t<Receiver, Direction>, py_backend, Receiver, Direction>::type {
            using export_base_t =
                export_object_base<object_t<Receiver, Direction>, py_backend, Receiver>;

        public:
            using object_ref::object_ref;

            template <
                typename... Args,
                typename D = Direction,
                std::enable_if_t<std::is_same<D, import_t>::value>* = nullptr>
            object_t(context_t& ctx, construct_object_t, Args&&... args);

            template <
                typename... Args,
                typename D = Direction,
                std::enable_if_t<std::is_same<D, export_t>::value>* = nullptr>
            object_t(context_t& ctx, PyObject* self, Args&&... args);

            template <typename Context, typename R = Receiver>
            R* native(Context& ctx) const;

            template <typename... Args>
            void construct_import_object_impl(context_t& ctx, Args&&... args);

            template <typename... Args>
            void construct_export_object_impl(context_t& ctx, PyObject* self, Args&&... args);

        private:
            struct native_holder {
                void* native = nullptr;
                void* ctx = nullptr;
                void (*destroy)(native_holder*) = nullptr;
            };

            static const char* native_attr_name() noexcept { return "__dynabridge_native__"; }

            template <typename Context, typename Exported>
            static void destroy_native_holder(native_holder* holder) noexcept {
                export_base_t::destroy_native(
                    *static_cast<Context*>(holder->ctx),
                    static_cast<Exported*>(holder->native));
                delete holder;
            }

            static void native_capsule_destructor(PyObject* capsule) noexcept {
                if (PyCapsule_IsValid(capsule, nullptr) == 0) {
                    return;
                }

                auto* holder = static_cast<native_holder*>(PyCapsule_GetPointer(capsule, nullptr));
                if (holder != nullptr && holder->destroy != nullptr) {
                    holder->destroy(holder);
                }
            }
        };

        template <typename R, std::enable_if_t<!is_void_v<R>>* = nullptr, typename... Args>
        static R invoke_impl(context_t& ctx, no_receiver_t, Args... args) {
            object_ref result(call(ctx, ctx.callable(), std::move(args)...), ref_policy::owned);
            if (!result) {
                throw std::runtime_error("Python callable returned null");
            }
            return from_cast<R>(ctx, result.get());
        }

        template <typename R, std::enable_if_t<is_void_v<R>>* = nullptr, typename... Args>
        static void invoke_impl(context_t& ctx, no_receiver_t, Args... args) {
            object_ref result(call(ctx, ctx.callable(), std::move(args)...), ref_policy::owned);
            if (!result) {
                throw std::runtime_error("Python callable returned null");
            }
        }

        template <
            typename R, typename Receiver, typename DynamicReceiver,
            std::enable_if_t<!is_void_v<R>>* = nullptr, typename... Args>
        static R invoke_impl(context_t& ctx, DynamicReceiver receiver, Args... args) {
            object_ref result(call(ctx, ctx.callable(), std::move(receiver), std::move(args)...),
                ref_policy::owned);
            if (!result) {
                throw std::runtime_error("Python member callable returned null");
            }
            return from_cast<R>(ctx, result.get());
        }

        template <
            typename R, typename Receiver, typename DynamicReceiver,
            std::enable_if_t<is_void_v<R>>* = nullptr, typename... Args>
        static void invoke_impl(context_t& ctx, DynamicReceiver receiver, Args... args) {
            object_ref result(call(ctx, ctx.callable(), std::move(receiver), std::move(args)...),
                ref_policy::owned);
            if (!result) {
                throw std::runtime_error("Python member callable returned null");
            }
        }

        template <typename Context, typename Target, typename Binder>
        static void define_impl(Context&, Target& target, const char* name, Binder binder) {
            target.define(name, make_callable(std::move(binder)));
        }

        template <typename Receiver, typename Context, typename Target>
        static class_target_t define_class_impl(Context&, Target& target, const char* name) {
            object_ref type_name(PyUnicode_FromString(name), ref_policy::owned);
            object_ref bases(PyTuple_Pack(1, reinterpret_cast<PyObject*>(&PyBaseObject_Type)),
                ref_policy::owned);
            object_ref dict(PyDict_New(), ref_policy::owned);
            object_ref type(PyObject_CallFunctionObjArgs(
                reinterpret_cast<PyObject*>(&PyType_Type),
                type_name.get(),
                bases.get(),
                dict.get(),
                nullptr), ref_policy::owned);

            if (!type) {
                throw std::runtime_error("creating Python class failed");
            }

            Py_INCREF(type.get());
            target.define(name, type.get());
            return class_target_t(type.release(), ref_policy::owned);
        }

        template <typename Receiver, typename Signature, typename Context>
        static void define_constructor_impl(Context& ctx, class_target_t& target) {
            target.define("__init__", make_constructor<Receiver, Signature>(ctx));
        }

        template <typename Class, typename Context>
        static void store_export_class_impl(Context& ctx, class_target_t target) {
            ctx.template store_export_class<Class>(std::move(target));
        }

        template <typename Class>
        static object_t<Class, export_t> bind_export_object_impl(context_t&, PyObject* self) {
            return object_t<Class, export_t>(self, ref_policy::borrowed);
        }

        template <typename Class, typename Context, typename... Args>
        static object_t<Class, export_t> make_export_object_impl(
            Context& ctx,
            Args&&... args)
        {
            class_target_t& target = ctx.template export_class<Class>();
            object_ref self(
                PyType_GenericNew(reinterpret_cast<PyTypeObject*>(target.get()), nullptr, nullptr),
                ref_policy::owned);
            if (!self) {
                throw std::runtime_error("PyType_GenericNew failed");
            }
            return object_t<Class, export_t>(ctx, self.get(), std::forward<Args>(args)...);
        }

        template <typename Class>
        static void define_export_instance_impl(
            context_t&,
            module_t& module,
            const char* name,
            object_t<Class, export_t> object)
        {
            module.define(name, object.release());
        }

        static PyObject* undefined(context_t&) {
            Py_INCREF(Py_None);
            return Py_None;
        }

        template <typename Symbol, typename Context>
        static Context import_impl(module_t& module, const char* name) {
            return Context(import_attr(module.get(), name), ref_policy::owned);
        }

        template <typename Symbol, typename Context>
        static Context import_impl(PyObject* module, const char* name) {
            return Context(import_attr(module, name), ref_policy::owned);
        }

        template <typename Symbol, typename Context>
        static Context import_impl(const char* module_name, const char* name) {
            object_ref module(PyImport_ImportModule(module_name), ref_policy::owned);
            if (!module) {
                throw std::runtime_error("PyImport_ImportModule failed");
            }
            return Context(import_attr(module.get(), name), ref_policy::owned);
        }

    private:
        static PyObject* import_attr(PyObject* module, const char* name) {
            PyObject* result = PyObject_GetAttrString(module, name);
            if (result == nullptr) {
                throw std::runtime_error("Python import lookup failed");
            }
            return result;
        }

        struct callable_holder {
            using tuple_invoke_fn_t = PyObject* (*)(callable_holder*, PyObject*);
#if PY_VERSION_HEX >= 0x03080000
            using vector_invoke_fn_t =
                PyObject* (*)(callable_holder*, PyObject* const*, std::size_t, PyObject*);
#endif
            using manager_fn_t = void (*)(callable_holder*, backend_lifecycle_op);

            callable_holder(
                tuple_invoke_fn_t tuple_invoke,
#if PY_VERSION_HEX >= 0x03080000
                vector_invoke_fn_t vector_invoke,
#endif
                manager_fn_t manager) noexcept
                : tuple_invoke_(tuple_invoke),
#if PY_VERSION_HEX >= 0x03080000
                  vector_invoke_(vector_invoke),
#endif
                  manager_(manager) {
            }

            PyObject* invoke_tuple(PyObject* args) { return tuple_invoke_(this, args); }

#if PY_VERSION_HEX >= 0x03080000
            PyObject* invoke_vector(PyObject* const* args, std::size_t nargsf, PyObject* kwnames) {
                return vector_invoke_(this, args, nargsf, kwnames);
            }
#endif

            void manage(backend_lifecycle_op op) noexcept { manager_(this, op); }

            void destroy() noexcept { manage(backend_lifecycle_op::destroy); }

            tuple_invoke_fn_t tuple_invoke_;
#if PY_VERSION_HEX >= 0x03080000
            vector_invoke_fn_t vector_invoke_;
#endif
            manager_fn_t manager_;
        };

        struct callable_object {
            PyObject_HEAD
            callable_holder* holder;
#if PY_VERSION_HEX >= 0x03080000
            vectorcallfunc vectorcall;
#endif
        };

        template <typename Binder>
        struct typed_callable_holder : callable_holder {
            explicit typed_callable_holder(Binder binder)
                : callable_holder(
                    tuple_call_entry,
#if PY_VERSION_HEX >= 0x03080000
                    vector_call_entry,
#endif
                    manage_entry),
                  binder_(std::move(binder)) {
            }

            static PyObject* tuple_call_entry(callable_holder* holder, PyObject* args) {
                return static_cast<typed_callable_holder*>(holder)->call_signature(
                    type_identity<typename Binder::signature_t>{}, args);
            }

#if PY_VERSION_HEX >= 0x03080000
            static PyObject* vector_call_entry(
                callable_holder* holder,
                PyObject* const* args,
                std::size_t nargsf,
                PyObject* kwnames) {
                return static_cast<typed_callable_holder*>(holder)->call_vector_signature(
                    type_identity<typename Binder::signature_t>{}, args, nargsf, kwnames);
            }
#endif

            static void manage_entry(callable_holder* holder, backend_lifecycle_op op) noexcept {
                if (op == backend_lifecycle_op::destroy) {
                    delete static_cast<typed_callable_holder*>(holder);
                }
            }

            template <typename R, typename... Args>
            PyObject* call_signature(type_identity<R(Args...)>, PyObject* args) {
                const Py_ssize_t actual = PyTuple_Check(args) ? PyTuple_GET_SIZE(args) : -1;
                if (actual != static_cast<Py_ssize_t>(sizeof...(Args))) {
                    PyErr_SetString(PyExc_TypeError, "dynabridge Python callback received wrong arity");
                    return nullptr;
                }

                try {
                    return call_indices<R>(args, std::index_sequence_for<Args...>{});
                } catch (const bad_conversion& error) {
                    PyErr_SetString(PyExc_TypeError, error.what());
                    return nullptr;
                } catch (const std::exception& error) {
                    PyErr_SetString(PyExc_RuntimeError, error.what());
                    return nullptr;
                } catch (...) {
                    PyErr_SetString(PyExc_RuntimeError, "dynabridge Python callback failed");
                    return nullptr;
                }
            }

#if PY_VERSION_HEX >= 0x03080000
            template <typename R, typename... Args>
            PyObject* call_vector_signature(
                type_identity<R(Args...)>,
                PyObject* const* args,
                std::size_t nargsf,
                PyObject* kwnames) {
                if (kwnames != nullptr && PyTuple_GET_SIZE(kwnames) != 0) {
                    PyErr_SetString(PyExc_TypeError,
                        "dynabridge Python callback does not accept keyword arguments");
                    return nullptr;
                }

                const Py_ssize_t actual = PyVectorcall_NARGS(nargsf);
                if (actual != static_cast<Py_ssize_t>(sizeof...(Args))) {
                    PyErr_SetString(PyExc_TypeError, "dynabridge Python callback received wrong arity");
                    return nullptr;
                }

                try {
                    return call_vector_indices<R>(args, std::index_sequence_for<Args...>{});
                } catch (const bad_conversion& error) {
                    PyErr_SetString(PyExc_TypeError, error.what());
                    return nullptr;
                } catch (const std::exception& error) {
                    PyErr_SetString(PyExc_RuntimeError, error.what());
                    return nullptr;
                } catch (...) {
                    PyErr_SetString(PyExc_RuntimeError, "dynabridge Python callback failed");
                    return nullptr;
                }
            }
#endif

            template <typename R, std::size_t... Indices, std::enable_if_t<!is_void_v<R>>* = nullptr>
            PyObject* call_indices(PyObject* args, std::index_sequence<Indices...>) {
                return binder_(PyTuple_GET_ITEM(args, Indices)...);
            }

            template <typename R, std::size_t... Indices, std::enable_if_t<is_void_v<R>>* = nullptr>
            PyObject* call_indices(PyObject* args, std::index_sequence<Indices...>) {
                binder_(PyTuple_GET_ITEM(args, Indices)...);
                Py_RETURN_NONE;
            }

#if PY_VERSION_HEX >= 0x03080000
            template <typename R, std::size_t... Indices, std::enable_if_t<!is_void_v<R>>* = nullptr>
            PyObject* call_vector_indices(PyObject* const* args, std::index_sequence<Indices...>) {
                return binder_(args[Indices]...);
            }

            template <typename R, std::size_t... Indices, std::enable_if_t<is_void_v<R>>* = nullptr>
            PyObject* call_vector_indices(PyObject* const* args, std::index_sequence<Indices...>) {
                binder_(args[Indices]...);
                Py_RETURN_NONE;
            }
#endif

            Binder binder_;
        };

        template <typename Binder>
        struct overload_callable_holder : callable_holder {
            explicit overload_callable_holder(Binder binder)
                : callable_holder(
                    tuple_call_entry,
#if PY_VERSION_HEX >= 0x03080000
                    vector_call_entry,
#endif
                    manage_entry),
                  binder_(std::move(binder)) {
            }

            struct tuple_accessor {
                constexpr static std::size_t static_arity = dynamic_arity;

                PyObject* args = nullptr;

                template <std::size_t I>
                PyObject* get() const {
                    return PyTuple_GET_ITEM(args, I);
                }
            };

#if PY_VERSION_HEX >= 0x03080000
            struct vector_accessor {
                constexpr static std::size_t static_arity = dynamic_arity;

                PyObject* const* args = nullptr;

                template <std::size_t I>
                PyObject* get() const {
                    return args[I];
                }
            };
#endif

            static PyObject* tuple_call_entry(callable_holder* holder, PyObject* args) {
                return static_cast<overload_callable_holder*>(holder)->call_tuple(args);
            }

#if PY_VERSION_HEX >= 0x03080000
            static PyObject* vector_call_entry(
                callable_holder* holder,
                PyObject* const* args,
                std::size_t nargsf,
                PyObject* kwnames) {
                return static_cast<overload_callable_holder*>(holder)->call_vector(args, nargsf, kwnames);
            }
#endif

            static void manage_entry(callable_holder* holder, backend_lifecycle_op op) noexcept {
                if (op == backend_lifecycle_op::destroy) {
                    delete static_cast<overload_callable_holder*>(holder);
                }
            }

            PyObject* call_tuple(PyObject* args) {
                if (!PyTuple_Check(args)) {
                    PyErr_SetString(PyExc_TypeError, "dynabridge Python callback expected tuple arguments");
                    return nullptr;
                }

                try {
                    auto result = binder_.dispatch_optional(
                        static_cast<std::size_t>(PyTuple_GET_SIZE(args)),
                        tuple_accessor{args});
                    if (!result) {
                        PyErr_SetString(PyExc_TypeError,
                            "dynabridge Python overload has no matching signature");
                        return nullptr;
                    }
                    return *result;
                } catch (const bad_conversion& error) {
                    PyErr_SetString(PyExc_TypeError, error.what());
                    return nullptr;
                } catch (const std::exception& error) {
                    PyErr_SetString(PyExc_RuntimeError, error.what());
                    return nullptr;
                } catch (...) {
                    PyErr_SetString(PyExc_RuntimeError, "dynabridge Python overload callback failed");
                    return nullptr;
                }
            }

#if PY_VERSION_HEX >= 0x03080000
            PyObject* call_vector(PyObject* const* args, std::size_t nargsf, PyObject* kwnames) {
                if (kwnames != nullptr && PyTuple_GET_SIZE(kwnames) != 0) {
                    PyErr_SetString(PyExc_TypeError,
                        "dynabridge Python callback does not accept keyword arguments");
                    return nullptr;
                }

                try {
                    auto result = binder_.dispatch_optional(
                        static_cast<std::size_t>(PyVectorcall_NARGS(nargsf)),
                        vector_accessor{args});
                    if (!result) {
                        PyErr_SetString(PyExc_TypeError,
                            "dynabridge Python overload has no matching signature");
                        return nullptr;
                    }
                    return *result;
                } catch (const bad_conversion& error) {
                    PyErr_SetString(PyExc_TypeError, error.what());
                    return nullptr;
                } catch (const std::exception& error) {
                    PyErr_SetString(PyExc_RuntimeError, error.what());
                    return nullptr;
                } catch (...) {
                    PyErr_SetString(PyExc_RuntimeError, "dynabridge Python overload callback failed");
                    return nullptr;
                }
            }
#endif

            Binder binder_;
        };

        template <typename Receiver, typename Signature, typename Context>
        struct class_constructor_holder;

        template <typename Receiver, typename Context, typename... Args>
        struct class_constructor_holder<Receiver, void(Args...), Context> : callable_holder {
            explicit class_constructor_holder(Context& ctx)
                : callable_holder(
                    tuple_call_entry,
#if PY_VERSION_HEX >= 0x03080000
                    vector_call_entry,
#endif
                    manage_entry),
                  ctx_(&ctx) {
            }

            static PyObject* tuple_call_entry(callable_holder* holder, PyObject* args) {
                return static_cast<class_constructor_holder*>(holder)->construct_tuple(args);
            }

#if PY_VERSION_HEX >= 0x03080000
            static PyObject* vector_call_entry(
                callable_holder* holder,
                PyObject* const* args,
                std::size_t nargsf,
                PyObject* kwnames) {
                return static_cast<class_constructor_holder*>(holder)->construct_vector(
                    args, nargsf, kwnames);
            }
#endif

            static void manage_entry(callable_holder* holder, backend_lifecycle_op op) noexcept {
                if (op == backend_lifecycle_op::destroy) {
                    delete static_cast<class_constructor_holder*>(holder);
                }
            }

            PyObject* construct_tuple(PyObject* args) {
                const Py_ssize_t actual = PyTuple_Check(args) ? PyTuple_GET_SIZE(args) : -1;
                if (actual != static_cast<Py_ssize_t>(sizeof...(Args) + 1)) {
                    PyErr_SetString(PyExc_TypeError, "dynabridge Python constructor received wrong arity");
                    return nullptr;
                }

                return construct_tuple_indices(args, std::index_sequence_for<Args...>{});
            }

#if PY_VERSION_HEX >= 0x03080000
            PyObject* construct_vector(
                PyObject* const* args,
                std::size_t nargsf,
                PyObject* kwnames) {
                if (kwnames != nullptr && PyTuple_GET_SIZE(kwnames) != 0) {
                    PyErr_SetString(PyExc_TypeError,
                        "dynabridge Python constructor does not accept keyword arguments");
                    return nullptr;
                }

                const Py_ssize_t actual = PyVectorcall_NARGS(nargsf);
                if (actual != static_cast<Py_ssize_t>(sizeof...(Args) + 1)) {
                    PyErr_SetString(PyExc_TypeError, "dynabridge Python constructor received wrong arity");
                    return nullptr;
                }

                return construct_vector_indices(args, std::index_sequence_for<Args...>{});
            }
#endif

            template <std::size_t... Indices>
            PyObject* construct_tuple_indices(PyObject* args, std::index_sequence<Indices...>) {
                return construct(
                    PyTuple_GET_ITEM(args, 0),
                    PyTuple_GET_ITEM(args, Indices + 1)...);
            }

#if PY_VERSION_HEX >= 0x03080000
            template <std::size_t... Indices>
            PyObject* construct_vector_indices(PyObject* const* args, std::index_sequence<Indices...>) {
                return construct(args[0], args[Indices + 1]...);
            }
#endif

            template <typename... DynamicArgs>
            PyObject* construct(PyObject* self, DynamicArgs... args) {
                try {
                    object_t<Receiver, export_t> object(*ctx_, self, from_cast<Args>(*ctx_, args)...);
                    (void)object;
                    Py_RETURN_NONE;
                } catch (const bad_conversion& error) {
                    PyErr_SetString(PyExc_TypeError, error.what());
                    return nullptr;
                } catch (const std::exception& error) {
                    PyErr_SetString(PyExc_RuntimeError, error.what());
                    return nullptr;
                } catch (...) {
                    PyErr_SetString(PyExc_RuntimeError, "dynabridge Python constructor failed");
                    return nullptr;
                }
            }

            Context* ctx_;
        };

        template <typename... Args>
        static PyObject* call(context_t&, PyObject* callable, Args... args) {
#if PY_VERSION_HEX >= 0x03080000
            return call_vector(callable, args...);
#else
            return call_tuple(callable, args...);
#endif
        }

#if PY_VERSION_HEX >= 0x03080000
        template <typename... Args>
        static PyObject* call_vector(PyObject* callable, Args... args) {
            if (callable == nullptr) {
                decref_args(args...);
                PyErr_SetString(PyExc_RuntimeError, "dynabridge Python context has no callable");
                return nullptr;
            }

            PyObject* argv[sizeof...(Args) == 0 ? 1 : sizeof...(Args)] = { args... };
            PyObject* result = PyObject_Vectorcall(callable, argv, sizeof...(Args), nullptr);
            decref_args(args...);
            return result;
        }
#endif

        template <typename... Args>
        static PyObject* call_tuple(PyObject* callable, Args... args) {
            if (callable == nullptr) {
                decref_args(args...);
                PyErr_SetString(PyExc_RuntimeError, "dynabridge Python context has no callable");
                return nullptr;
            }

            object_ref tuple(PyTuple_New(sizeof...(Args)), ref_policy::owned);
            if (!tuple) {
                decref_args(args...);
                return nullptr;
            }

            fill_tuple(tuple.get(), 0, args...);
            return PyObject_CallObject(callable, tuple.get());
        }

        static void fill_tuple(PyObject*, Py_ssize_t) noexcept {}

        template <typename Head, typename... Tail>
        static void fill_tuple(PyObject* tuple, Py_ssize_t index, Head head, Tail... tail) {
            PyTuple_SET_ITEM(tuple, index, head);
            fill_tuple(tuple, index + 1, tail...);
        }

        static void decref_args() noexcept {}

        template <typename Head, typename... Tail>
        static void decref_args(Head head, Tail... tail) noexcept {
            Py_XDECREF(head);
            decref_args(tail...);
        }

        static PyObject* make_callable_object(callable_holder* holder) {
            PyTypeObject* type = callable_type();
            if (type == nullptr) {
                holder->destroy();
                return nullptr;
            }

            callable_object* object = PyObject_New(callable_object, type);
            if (object == nullptr) {
                holder->destroy();
                return nullptr;
            }

            object->holder = holder;
#if PY_VERSION_HEX >= 0x03080000
            object->vectorcall = callable_vectorcall;
#endif
            return reinterpret_cast<PyObject*>(object);
        }

        template <typename Binder>
        static PyObject* make_callable(Binder binder) {
            using binder_t = typename std::decay<Binder>::type;
            return make_callable_impl(std::move(binder), is_export_overload_binder<binder_t>{});
        }

        template <typename Binder>
        static PyObject* make_callable_impl(Binder binder, std::false_type) {
            return make_callable_object(new typed_callable_holder<typename std::decay<Binder>::type>(
                std::move(binder)));
        }

        template <typename Binder>
        static PyObject* make_callable_impl(Binder binder, std::true_type) {
            return make_callable_object(new overload_callable_holder<typename std::decay<Binder>::type>(
                std::move(binder)));
        }

        template <typename Receiver, typename Signature, typename Context>
        static PyObject* make_constructor(Context& ctx) {
            return make_callable_object(new class_constructor_holder<Receiver, Signature, Context>(ctx));
        }

        static PyObject* callable_call(PyObject* self, PyObject* args, PyObject* kwargs) {
            if (kwargs != nullptr && PyDict_Size(kwargs) != 0) {
                PyErr_SetString(PyExc_TypeError,
                    "dynabridge Python callback does not accept keyword arguments");
                return nullptr;
            }
            return reinterpret_cast<callable_object*>(self)->holder->invoke_tuple(args);
        }

#if PY_VERSION_HEX >= 0x03080000
        static PyObject* callable_vectorcall(
            PyObject* self,
            PyObject* const* args,
            std::size_t nargsf,
            PyObject* kwnames) {
            return reinterpret_cast<callable_object*>(self)->holder->invoke_vector(
                args, nargsf, kwnames);
        }
#endif

        static PyObject* callable_descr_get(PyObject* self, PyObject* object, PyObject*) {
            if (object == nullptr || object == Py_None) {
                Py_INCREF(self);
                return self;
            }
            return PyMethod_New(self, object);
        }

        static void callable_dealloc(PyObject* self) {
            callable_holder* holder = reinterpret_cast<callable_object*>(self)->holder;
            if (holder != nullptr) {
                holder->destroy();
            }
            Py_TYPE(self)->tp_free(self);
        }

        static PyTypeObject* callable_type() {
            static PyTypeObject type = {
                PyVarObject_HEAD_INIT(nullptr, 0)
            };
            static bool ready = false;

            if (!ready) {
                type.tp_name = "dynabridge.PyCallable";
                type.tp_basicsize = sizeof(callable_object);
#if PY_VERSION_HEX >= 0x03080000
                type.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_VECTORCALL;
                type.tp_call = PyVectorcall_Call;
                type.tp_vectorcall_offset = static_cast<Py_ssize_t>(offsetof(callable_object, vectorcall));
#else
                type.tp_flags = Py_TPFLAGS_DEFAULT;
                type.tp_call = callable_call;
#endif
                type.tp_descr_get = callable_descr_get;
                type.tp_dealloc = callable_dealloc;

                if (PyType_Ready(&type) != 0) {
                    return nullptr;
                }
                ready = true;
            }

            return &type;
        }
    };

    template <typename Receiver, typename Direction>
    template <
        typename... Args,
        typename D,
        std::enable_if_t<std::is_same<D, import_t>::value>*>
    py_backend::object_t<Receiver, Direction>::object_t(
        context_t& ctx,
        construct_object_t,
        Args&&... args) {
        this->construct_import_object(ctx, std::forward<Args>(args)...);
    }

    template <typename Receiver, typename Direction>
    template <
        typename... Args,
        typename D,
        std::enable_if_t<std::is_same<D, export_t>::value>*>
    py_backend::object_t<Receiver, Direction>::object_t(
        context_t& ctx,
        PyObject* self,
        Args&&... args) {
        this->construct_export_object(ctx, self, std::forward<Args>(args)...);
    }

    template <typename Receiver, typename Direction>
    template <typename Context, typename R>
    R* py_backend::object_t<Receiver, Direction>::native(Context&) const {
        if (get() == nullptr) {
            return nullptr;
        }

        object_ref capsule(PyObject_GetAttrString(get(), native_attr_name()), ref_policy::owned);
        if (!capsule) {
            PyErr_Clear();
            return nullptr;
        }

        if (PyCapsule_IsValid(capsule.get(), nullptr) == 0) {
            PyErr_Clear();
            return nullptr;
        }

        auto* holder = static_cast<native_holder*>(PyCapsule_GetPointer(capsule.get(), nullptr));
        if (holder == nullptr) {
            PyErr_Clear();
            return nullptr;
        }
        return static_cast<R*>(holder->native);
    }

    template <typename Receiver, typename Direction>
    template <typename... Args>
    void py_backend::object_t<Receiver, Direction>::construct_import_object_impl(
        context_t& ctx,
        Args&&... args) {
        object_ref::reset(py_backend::call(
                ctx, ctx.callable(),
                to_cast<typename std::decay<Args>::type>(ctx, std::forward<Args>(args))...),
            ref_policy::owned);
        if (!get()) {
            throw std::runtime_error("Python constructor returned null");
        }
    }

    template <typename Receiver, typename Direction>
    template <typename... Args>
    void py_backend::object_t<Receiver, Direction>::construct_export_object_impl(
        context_t& ctx,
        PyObject* self,
        Args&&... args) {
        using receiver_t = Receiver;
        native_holder* holder = new native_holder;
        holder->ctx = &ctx;
        holder->destroy = destroy_native_holder<context_t, receiver_t>;
        try {
            holder->native = export_base_t::construct_native(ctx, std::forward<Args>(args)...);
        } catch (...) {
            delete holder;
            throw;
        }

        object_ref capsule(PyCapsule_New(holder, nullptr, native_capsule_destructor), ref_policy::owned);
        if (!capsule) {
            holder->destroy(holder);
            throw std::runtime_error("PyCapsule_New failed");
        }

        if (PyObject_SetAttrString(self, native_attr_name(), capsule.get()) != 0) {
            throw std::runtime_error("PyObject_SetAttrString failed");
        }

        object_ref::reset(self, ref_policy::borrowed);
    }
}

#include "python_converters.h"

#endif //DYNABRIDGE_BACKENDS_PYTHON_H
