#include "../../dynabridge/async_import.h"
int main(){
#if ASYNC_CASE == 1
    int n=0;auto invalid=dynabridge::async_detail::capture(&n);(void)invalid;
#elif ASYNC_CASE == 2
    const std::string s="borrowed";auto copy=dynabridge::async_detail::capture(s);(void)copy;
#elif ASYNC_CASE == 3
    auto call=dynabridge::make_invocation<int>(std::make_shared<int>(1),[](int&,auto done){done.finish(dynabridge::async_result<int>(1));return dynabridge::async_submission::accept();});
    std::move(call).start([](dynabridge::async_result<int>) noexcept {});
#else
    auto owned=dynabridge::async_detail::capture(std::string("owned"));(void)owned;
#endif
}
