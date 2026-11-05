#include <dynabridge/async_import.h>
#include <cstdio>
#define DYNABRIDGE_IMPORT_DEF "tests/async_import.def"
#include <dynabridge/import.h>
#undef DYNABRIDGE_IMPORT_DEF
#define CHECK(x) do {if(!(x))std::abort();}while(false)
struct backend:dynabridge::backend_base<backend> {
    struct context {using backend_t=backend;int number=10;};
    template<class C,class D> struct object_t {int identity;};
    template<class Symbol,class Contract,class Receiver,class Tuple>
    static auto prepare_async(std::shared_ptr<Receiver> receiver,Tuple args) {
        return dynabridge::make_invocation<int>(std::move(receiver),[args=std::move(args)](Receiver& r,auto done) noexcept {
            int n=r.context().number+sum(args);
            done.finish(dynabridge::async_result<int>(n));return dynabridge::async_submission::accept();
        });
    }
    static int sum(const std::tuple<>&) noexcept{return 0;}
    static int sum(const std::tuple<int>& t) noexcept{return std::get<0>(t);}
    template<class Symbol,class Contract,class Tuple>
    static auto prepare_async_free(std::shared_ptr<context> c,Tuple args) {
        static_assert(std::is_same<Contract,dynabridge::free_callable<std::string(std::string&&)>>::value,"original rvalue overload must remain selected");
        return dynabridge::make_invocation<std::string>(std::move(c),[args=std::move(args)](context&,auto done) mutable noexcept {
            done.finish(dynabridge::async_result<std::string>(std::move(std::get<0>(args))));return dynabridge::async_submission::accept();
        });
    }
};
int main(){
    auto owner=std::make_shared<backend::context>();
    using D=dynabridge::async_counter<backend::context>;
    const auto view=dynabridge::bind_async<dynabridge::async_counter>(owner,D::object_t{1});
    const auto static_view=dynabridge::bind_static_async<dynabridge::async_counter>(owner,D::object_t{1});
    int n=0;
    std::move(view.add(4)).start([&](dynabridge::async_result<int> r) noexcept{n=*r.value;});CHECK(n==14);
    std::move(static_view.value()).start([&](dynabridge::async_result<int> r) noexcept{n=*r.value;});CHECK(n==10);
    auto echo=dynabridge::async_functions::async_echo(owner,std::string("owned"));
    std::move(echo).start([](dynabridge::async_result<std::string> r) noexcept{CHECK(*r.value=="owned");});
    std::puts("async projections passed");
}
