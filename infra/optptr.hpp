#ifndef OPTPTR_INCLUDE_GUARD
#define OPTPTR_INCLUDE_GUARD

#include "common.hpp"

template<typename T>
struct OptPtr
{
	T* ptr;
};

template<typename T>
static inline bool operator==(OptPtr<T> a, OptPtr<T> b) noexcept
{
	return a.ptr == b.ptr;
}

template<typename T>
static inline bool operator!=(OptPtr<T> a, OptPtr<T> b) noexcept
{
	return a.ptr != b.ptr;
}


template<typename T>
static inline OptPtr<T> none() noexcept
{
	return { nullptr };
}

template<typename T>
static inline OptPtr<T> some(T* value) noexcept
{
	ASSERT_OR_IGNORE(value != nullptr);

	return { value };
}

template<typename T>
static inline OptPtr<T> maybe(T* value) noexcept
{
	return { value };
}

template<typename T>
static inline bool is_none(OptPtr<T> ptr) noexcept
{
	return ptr.ptr == nullptr;
}

template<typename T>
static inline bool is_some(OptPtr<T> ptr) noexcept
{
	return ptr.ptr != nullptr;
}

template<typename T>
static inline T& get(OptPtr<T> ptr) noexcept
{
	ASSERT_OR_IGNORE(is_some(ptr));

	return *ptr.ptr;
}

template<typename T>
static inline T* get_ptr(OptPtr<T> ptr) noexcept
{
	ASSERT_OR_IGNORE(is_some(ptr));

	return ptr.ptr;
}

#endif // OPTPTR_INCLUDE_GUARD
