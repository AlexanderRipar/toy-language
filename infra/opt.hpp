#ifndef OPT_INCLUDE_GUARD
#define OPT_INCLUDE_GUARD

#include "assert.hpp"
#include "template_helpers.hpp"

template<typename T>
struct Maybe
{
	T t;

	Maybe() noexcept = default;

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

	template<typename Enable_ = std::enable_if_t<is_ptr_type<T>>>
	operator Maybe<const_ptr_type<T>>() const noexcept
	{
		return Maybe<const_ptr_type<T>>{ t };
	}

private:

	Maybe(T t) noexcept : t{ t } {}
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
