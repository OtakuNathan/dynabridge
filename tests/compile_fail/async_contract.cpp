#include "../../dynabridge/async_import.h"
#if ASYNC_CASE == 8
struct throwing_storage {
    throwing_storage()=default;
    throwing_storage(throwing_storage&&) noexcept(false) {}
};
namespace dynabridge {
template<> struct async_capture<throwing_storage> {
    static throwing_storage capture(throwing_storage&&) noexcept {return throwing_storage{};}
};
}
#endif
int main(){
#if ASYNC_CASE == 1
    int n=0;auto invalid=dynabridge::async_detail::capture(&n);(void)invalid;
#elif ASYNC_CASE == 2
    const std::string s="borrowed";auto copy=dynabridge::async_detail::capture(s);(void)copy;
#elif ASYNC_CASE == 3
    auto call=dynabridge::make_invocation<int>(std::make_shared<int>(1),[](int&,auto done){done.finish(dynabridge::async_result<int>(1));return dynabridge::async_submission::accept();});
    std::move(call).start([](dynabridge::async_result<int>) noexcept {});
#elif ASYNC_CASE == 4
    auto wrong=dynabridge::make_invocation<double>(std::make_shared<int>(1),[](int&,auto done) noexcept {done.finish(dynabridge::async_result<double>(1.0));return dynabridge::async_submission::accept();});
    auto invalid=dynabridge::async_detail::checked_projection<false,dynabridge::free_callable<int()>>(std::move(wrong));(void)invalid;
#elif ASYNC_CASE == 5
    auto call=dynabridge::erase_invocation(dynabridge::make_invocation<int>(std::make_shared<int>(1),[](int&,auto done) noexcept {done.finish(dynabridge::async_result<int>(1));return dynabridge::async_submission::accept();}));
    std::move(call).start([](dynabridge::async_result<int>) {});
#elif ASYNC_CASE == 6
    using invalid=dynabridge::async_detail::contract_result<dynabridge::free_callable<int(int&)>>::type;
    invalid value=0;(void)value;
#elif ASYNC_CASE == 7
    struct handler {
        handler()=default;
        handler(const handler&) noexcept(false) {}
        handler(handler&&) noexcept=default;
        void operator()(dynabridge::async_result<int>) noexcept {}
    } callback;
    auto call=dynabridge::make_invocation<int>(std::make_shared<int>(1),[](int&,auto done) noexcept {done.finish(dynabridge::async_result<int>(1));return dynabridge::async_submission::accept();});
    std::move(call).start(callback);
#elif ASYNC_CASE == 8
    auto invalid=dynabridge::async_detail::capture(throwing_storage{});(void)invalid;
#else
    auto owned=dynabridge::async_detail::capture(std::string("owned"));(void)owned;
#endif
}
