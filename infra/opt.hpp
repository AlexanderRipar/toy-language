#ifndef OPT_INCLUDE_GUARD
#define OPT_INCLUDE_GUARD

#include "common.hpp"
#include <type_traits>

template<typename T>
struct MemberDetector : std::true_type {};

template<typename T>
struct HasInvalidValue
{
	static constexpr bool value = MemberDetector<decltype(T::INVALID)>::value;
};

template<typename T>
struct HasInvalidValue<T*>
{
	static constexpr bool value = true;
};

template<typename T>
struct Maybe
{
	T t;

	bool is_none_() const noexcept
	{
		if constexpr (is_ptr_type<T>)
			return t == nullptr;
		else
			return t == T::INVALID;
	}

	static bool is_invalid_t_(T t) noexcept
	{
		if constexpr (is_ptr_type<T>)
			return t == nullptr;
		else
			return t == T::INVALID;
	}

	static Maybe<T> make_none_() noexcept
	{
		if constexpr (is_ptr_type<T>)
			return { nullptr };
		else
			return { T::INVALID };
	}

	static Maybe<T> make_some_(T t) noexcept
	{
		ASSERT_OR_IGNORE(!is_invalid_t_(t));

		return { t };
	}

	bool operator==(const Maybe<T>& rhs) const noexcept
	{
		return t == rhs.t;
	}

	bool operator!=(const Maybe<T>& rhs) const noexcept
	{
		return t != rhs.t;
	}
};

template<typename T>
static inline Maybe<T> none() noexcept
{
	return Maybe<T>::make_none_();
}

template<typename T>
static inline Maybe<T> some(T value) noexcept
{
	return Maybe<T>::make_some_(value);
}

template<typename T>
static inline bool is_none(Maybe<T> opt) noexcept
{
	return opt.is_none_();
}

template<typename T>
static inline bool is_some(Maybe<T> opt) noexcept
{
	return !opt.is_none_();
}

template<typename T>
static inline T get(Maybe<T> opt) noexcept
{
	ASSERT_OR_IGNORE(!opt.is_none_());

	return opt.t;
}

#endif // OPT_INCLUDE_GUARD
