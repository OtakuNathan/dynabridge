#pragma once
#include <cstddef>
#include <cstdlib>
#include <new>
#ifdef PORTAL_COUNT_ALLOCATIONS
static thread_local bool count_enabled=false;
static thread_local std::size_t allocations=0;
#if defined(__GNUC__) || defined(__clang__)
#define NOINLINE __attribute__((noinline))
#else
#define NOINLINE __declspec(noinline)
#endif
NOINLINE void* operator new(std::size_t n) {
    if(count_enabled)++allocations;
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
NOINLINE void operator delete(void* p) noexcept {std::free(p);}
NOINLINE void operator delete(void* p,std::size_t) noexcept {std::free(p);}
NOINLINE void* operator new[](std::size_t n) {return ::operator new(n);}
NOINLINE void operator delete[](void* p) noexcept {::operator delete(p);}
NOINLINE void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
#endif
