#ifndef UTIL_REF_INCLUDE_GUARD
#define UTIL_REF_INCLUDE_GUARD

template<typename T>
struct refptr
{
	T* ptr = nullptr;

	refptr() noexcept = default;

	refptr(T* ptr) noexcept : ptr{ ptr } {}

	const T& operator*() const noexcept
	{
		return *ptr;
	}

	T& operator*() noexcept
	{
		return *ptr;
	}

	const T* operator->() const noexcept
	{
		return ptr;
	}
	
	T* operator->() noexcept
	{
		return ptr;
	}
};

#endif // UTIL_REF_INCLUDE_GUARD
