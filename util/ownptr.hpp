#ifndef UTIL_OWNED_INCLUDE_GUARD
#define UTIL_OWNED_INCLUDE_GUARD

#include <cassert>

template<typename T>
struct ownptr
{
	T* ptr = nullptr;

	ownptr() noexcept = default;

	ownptr(T* ptr) noexcept : ptr{ ptr } {}

	bool alloc() noexcept
	{
		assert(ptr == nullptr);

		ptr = static_cast<T*>(malloc(sizeof(T)));

		return ptr != nullptr;
	}

	T* exchange(T* new_ptr) noexcept
	{
		assert(ptr != nullptr)

		T* tmp = ptr;

		ptr = new_ptr;

		return tmp;
	}

	void free() noexcept
	{
		free(ptr);

		ptr = nullptr;
	}

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

	~ownptr() noexcept
	{
		this.free();
	}
};

template<typename T>
struct ownoptptr
{
	T* ptr = nullptr;

	ownoptptr() noexcept = default;

	ownoptptr(T* ptr) noexcept : ptr{ ptr } {}

	bool alloc() noexcept
	{
		assert(ptr == nullptr);

		ptr = static_cast<T*>(malloc(sizeof(T)));

		return ptr != nullptr;
	}

	operator bool() const noexcept
	{
		return ptr != nullptr;
	}

	T* release() noexcept
	{
		T* tmp = ptr;

		ptr = nullptr;

		return tmp;
	}

	void free() noexcept
	{
		free(ptr);

		ptr = nullptr;
	}

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

	~ownoptptr() noexcept
	{
		this.free();
	}
};

#endif // UTIL_OWNED_INCLUDE_GUARD