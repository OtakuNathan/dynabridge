#include <dynabridge/extensions/flux_foundry/async.h>
#include <cstdlib>
#define CHECK(x) do {if(!(x))std::abort();}while(false)
struct payload {
    int value;
    explicit payload(int n) noexcept:value(n){}
    payload()=delete;payload(const payload&)=delete;
    payload(payload&&) noexcept=default;payload& operator=(payload&&) noexcept=default;
};
struct receiver {
    using value_type=flux_foundry::result_t<payload,dynabridge::async_error>;
    std::shared_ptr<int> count;
    void emplace(value_type&& r) noexcept {CHECK(r.has_value()&&r.value().value==42);++*count;}
};
int main(){
    static_assert(std::is_nothrow_constructible<dynabridge::async_error,std::exception_ptr>::value,"FF exception conversion");
    auto owner=std::make_shared<int>(41);int count=0;
    auto make=[&]{return dynabridge::make_invocation<payload>(owner,[](int& n,auto done) noexcept {done.finish(dynabridge::async_result<payload>(payload(n+1)));return dynabridge::async_submission::accept();});};
    auto handler=[&](flux_foundry::result_t<payload,dynabridge::async_error> r) noexcept {CHECK(r.has_value()&&r.value().value==42);++count;};
    dynabridge::ff::start_invocation(make(),handler);
    dynabridge::ff::start_invocation(dynabridge::erase_invocation(make()),handler);
    auto failed=dynabridge::make_invocation<int>(owner,[](int&,auto) noexcept{return dynabridge::async_submission::reject(std::make_error_code(std::errc::device_or_resource_busy));});
    dynabridge::ff::start_invocation(std::move(failed),[&](flux_foundry::result_t<int,dynabridge::async_error> r) noexcept {CHECK(!r.has_value()&&r.error().code==std::errc::device_or_resource_busy);++count;});
    CHECK(count==3);
    using Call=decltype(make());using Output=receiver::value_type;
    auto bp=flux_foundry::make_blueprint<Call,dynabridge::async_error>() | dynabridge::ff::await_static_invocation<Call>()
        | flux_foundry::end([](Output&& r) noexcept -> Output {return std::move(r);});
    auto completed=std::make_shared<int>(0);
    auto shared_bp=flux_foundry::make_lite_ptr<decltype(bp)>(std::move(bp));
    for(int i=0;i<2;++i){auto runner=flux_foundry::make_runner(shared_bp,receiver{completed});runner(make());}
    CHECK(*completed==2);
}
