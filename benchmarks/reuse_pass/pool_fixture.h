#ifndef PORTAL_REUSE_POOL_FIXTURE_H
#define PORTAL_REUSE_POOL_FIXTURE_H
// Benchmark/test fixture, NOT a general-purpose production allocator.
// A bounded, synchronized exact-size cache. Retained resource ownership remains
// valid across final-token release on another thread. Overaligned types are
// deliberately rejected instead of being silently misaligned in C++14.
#include "portal/error_policy.h"
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <memory>
#include <mutex>
#include <new>

namespace reuse_test {
class pool_resource {
    struct alignas(std::max_align_t) block { block* next; std::size_t size; };
    std::mutex mutex_;
    block* free_=nullptr;
    std::size_t cached_=0, limit_;
public:
    std::atomic<std::size_t> upstream{0}, outstanding{0};
    bool fail_next=false; // test-only, set when there are no concurrent users
    explicit pool_resource(std::size_t limit=64):limit_(limit){}
    pool_resource(const pool_resource&)=delete;
    ~pool_resource(){
        if(outstanding.load()!=0)std::abort();
        while(free_){auto* b=free_;free_=b->next;::operator delete(b);}
    }
    void* allocate(std::size_t size) {
        #ifdef PORTAL_POOL_COUNT
        if(fail_next) {fail_next=false;fail();}
#endif
        {
            std::lock_guard<std::mutex> guard(mutex_);
            block** p=&free_;
            while(*p) {
                if((*p)->size==size) {
                    auto* b=*p;*p=b->next;--cached_;
#ifdef PORTAL_POOL_COUNT
                    ++outstanding;
#endif
                    return b+1;
                }
                p=&(*p)->next;
            }
        }
        if(size>std::numeric_limits<std::size_t>::max()-sizeof(block))fail();
        auto* b=static_cast<block*>(::operator new(sizeof(block)+size));
        b->size=size;b->next=nullptr;
#ifdef PORTAL_POOL_COUNT
        ++upstream;++outstanding;
#endif
        return b+1;
    }
    void deallocate(void* p) noexcept {
        auto* b=static_cast<block*>(p)-1;
#ifdef PORTAL_POOL_COUNT
        --outstanding;
#endif
        {
            std::lock_guard<std::mutex> guard(mutex_);
            if(cached_<limit_){b->next=free_;free_=b;++cached_;return;}
        }
        ::operator delete(b);
    }
    [[noreturn]] static void fail() {
#if PORTAL_HAS_EXCEPTIONS
        throw std::bad_alloc();
#else
        std::abort();
#endif
    }
};
template<class T> struct pool_allocator {
    using value_type=T;
    std::shared_ptr<pool_resource> resource;
    explicit pool_allocator(std::shared_ptr<pool_resource> r) noexcept:resource(std::move(r)){}
    template<class U> pool_allocator(const pool_allocator<U>& a) noexcept:resource(a.resource){}
    T* allocate(std::size_t n) {
        static_assert(alignof(T)<=alignof(std::max_align_t),"benchmark pool: over-aligned type unsupported");
        if(n>std::numeric_limits<std::size_t>::max()/sizeof(T))pool_resource::fail();
        return static_cast<T*>(resource->allocate(n*sizeof(T)));
    }
    void deallocate(T* p,std::size_t) noexcept {resource->deallocate(p);}
    template<class U> bool operator==(const pool_allocator<U>& b) const noexcept {return resource==b.resource;}
    template<class U> bool operator!=(const pool_allocator<U>& b) const noexcept {return !(*this==b);}
};
}
#endif
