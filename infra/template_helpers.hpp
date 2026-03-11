#ifndef TEMPLATE_HELPERS_INCLUDE_GUARD
#define TEMPLATE_HELPERS_INCLUDE_GUARD

template<typename, typename>
constexpr bool is_same_cpp_type = false;
template <typename T>
constexpr bool is_same_cpp_type<T, T> = true;

template<typename T>
constexpr bool is_ptr_type = false;
template<typename T>
constexpr bool is_ptr_type<T*> = true;
template<typename T>
constexpr bool is_ptr_type<T* const> = true;
template<typename T>
constexpr bool is_ptr_type<T* volatile> = true;
template<typename T>
constexpr bool is_ptr_type<T* const volatile> = true;

template<typename T>
struct ConstexprPtrTypeHelper
{
	using type = void;
};

template<typename T>
struct ConstexprPtrTypeHelper<T*>
{
	using type = const T*;
};

template<typename T>
struct ConstexprPtrTypeHelper<const T*>
{
	using type = const T*;
};

template<typename T>
struct ConstexprPtrTypeHelper<volatile T*>
{
	using type = const volatile T*;
};

template<typename T>
struct ConstexprPtrTypeHelper<const volatile T*>
{
	using type = const volatile T*;
};

template<typename T>
using const_ptr_type = typename ConstexprPtrTypeHelper<T>::type;

#endif // TEMPLATE_HELPERS_INCLUDE_GUARD
