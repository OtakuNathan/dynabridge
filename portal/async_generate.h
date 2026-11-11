// Replayed with the same declaration table; no new method identifiers are allocated.
#include "async_import.h"
namespace portal { namespace async_delegates {
#define BEGIN_INTERFACE(name) template<class Host> struct name {
#define END_INTERFACE };
#define IMPLEMENTS(name)
#define BEGIN_CLASS(clazz) template<class Context,bool Erased> class clazz;
#define END_CLASS
#define BEGIN_CALLABLE_GROUP(name)
#define DECL_CALLABLE(...)
#define DECL_FUNCTION(...)
#define END_CALLABLE_GROUP
#define DECL_CONSTRUCTOR(...)
#define BEGIN_MEMBER_CALLABLE_GROUP(name) template<class... Args> auto name(Args&&... args) const { \
    using Symbol=typename Host::bridge_symbol_t::name; \
    return static_cast<const Host*>(this)->template prepare<Symbol>(std::forward<Args>(args)...); }
#define DECL_MEMBER_FUNCTION(...)
#define END_MEMBER_CALLABLE_GROUP
// Only interface bodies in this pass; class methods need to be suppressed separately.
#undef BEGIN_CLASS
#undef END_CLASS
#define BEGIN_CLASS(clazz) template<class Host> struct ignored_##clazz {
#define END_CLASS };
#include PORTAL_IMPORT_DEF
#undef BEGIN_INTERFACE
#undef END_INTERFACE
#undef BEGIN_CLASS
#undef END_CLASS
#undef BEGIN_MEMBER_CALLABLE_GROUP
}
namespace async_detail {
#define BEGIN_INTERFACE(name) template<class Host> struct mixin<interface_descriptors::name<import_t>,Host> : async_delegates::name<Host> {};
#define END_INTERFACE
#define BEGIN_CLASS(clazz)
#define END_CLASS
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#include PORTAL_IMPORT_DEF
#undef BEGIN_INTERFACE
#undef END_INTERFACE
#undef BEGIN_CLASS
#undef END_CLASS
#undef BEGIN_MEMBER_CALLABLE_GROUP
}
namespace async_delegates {
#define BEGIN_INTERFACE(name)
#define END_INTERFACE
// Interface methods must not be emitted at namespace scope: use ignored templates.
#undef BEGIN_INTERFACE
#undef END_INTERFACE
#define BEGIN_INTERFACE(name) template<class Context,bool Erased> struct ignored_interface_##name : Context { using bridge_symbol_t=interface_descriptors::name<import_t>::template symbols<void>;
#define END_INTERFACE };
#define BEGIN_CLASS(clazz) template<class Context,bool Erased> class clazz \
    : public async_detail::receiver_storage<portal::clazz<Context>,Erased>, \
      public async_detail::interfaces<clazz<Context,Erased>,typename class_interfaces<import_symbols::clazz>::type> { \
    using Base=async_detail::receiver_storage<portal::clazz<Context>,Erased>; \
    public: using Base::Base; using bridge_symbol_t=import_symbols::clazz;
#define END_CLASS };
#define BEGIN_MEMBER_CALLABLE_GROUP(name) template<class... Args> auto name(Args&&... args) const { \
    return this->template prepare<typename bridge_symbol_t::name>(std::forward<Args>(args)...); }
#include PORTAL_IMPORT_DEF
#undef BEGIN_INTERFACE
#undef END_INTERFACE
#undef BEGIN_CLASS
#undef END_CLASS
#undef BEGIN_MEMBER_CALLABLE_GROUP
}
#define BEGIN_INTERFACE(name)
#define END_INTERFACE
#define BEGIN_CLASS(clazz) template<> struct async_class_traits<portal::clazz> { \
    template<bool Erased,class C,class O> static auto bind(std::shared_ptr<C> owner,O&& object) { \
        return async_delegates::clazz<C,Erased>(std::move(owner),std::forward<O>(object)); } };
#define END_CLASS
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#include PORTAL_IMPORT_DEF
#undef BEGIN_INTERFACE
#undef END_INTERFACE
#undef BEGIN_CLASS
#undef END_CLASS
#undef BEGIN_MEMBER_CALLABLE_GROUP
#undef BEGIN_CALLABLE_GROUP
#undef END_CALLABLE_GROUP
namespace async_functions {
#define BEGIN_INTERFACE(name)
#define END_INTERFACE
#define BEGIN_CLASS(clazz)
#define END_CLASS
#define BEGIN_MEMBER_CALLABLE_GROUP(name)
#define BEGIN_CALLABLE_GROUP(name) template<bool Erased=true,class C,class... A> auto name(std::shared_ptr<C> owner,A&&... args) { \
    async_detail::require(bool(owner)); \
    using Symbol=import_symbols::name; \
    using Selector=import_contract_selector_group<typename Symbol::overloads_t,C>; \
    using Contract=typename decltype(Selector::select(std::declval<A>()...))::type; \
    auto captured=std::make_tuple(async_detail::capture(std::forward<A>(args))...); \
    return async_detail::checked_projection<Erased,Contract>(C::backend_t::template prepare_async_free<Symbol,Contract>(std::move(owner),std::move(captured))); }
#define END_CALLABLE_GROUP
#include PORTAL_IMPORT_DEF
#undef BEGIN_INTERFACE
#undef END_INTERFACE
#undef BEGIN_CLASS
#undef END_CLASS
#undef BEGIN_MEMBER_CALLABLE_GROUP
}
#undef IMPLEMENTS
#undef DECL_CONSTRUCTOR
#undef DECL_MEMBER_FUNCTION
#undef END_MEMBER_CALLABLE_GROUP
#undef BEGIN_CALLABLE_GROUP
#undef DECL_CALLABLE
#undef DECL_FUNCTION
#undef END_CALLABLE_GROUP
}
