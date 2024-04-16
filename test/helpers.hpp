#ifndef HELPERS_INCLUDE_GUARD
#define HELPERS_INCLUDE_GUARD

#include "../common.hpp"
#include "../range.hpp"
#include "../minos.hpp"
#include <cstdio>
#include <cstdarg>
#include <type_traits>

template<typename T>
using thread_proc = void (*) (T* arg, u32 thread_id, u32 thread_count);

template<typename T>
void run_on_threads_and_wait(u32 thread_count, thread_proc<T> proc, T* arg) noexcept
{
	run_on_threads_and_wait_impl_(thread_count, reinterpret_cast<thread_proc_impl_>(proc), arg);
}


using thread_proc_impl_ = void (*) (void* arg, u32 thread_id, u32 thread_count);

void run_on_threads_and_wait_impl_(u32 thread_count, thread_proc_impl_ proc, void* arg) noexcept;

enum class LogLevel
{
	Info,
	Failure,
	Fatal,
};

void log(LogLevel level, const char8* fmt, ...) noexcept;

void add_error() noexcept;

void test_system_init(u32 argc, const char8** argv) noexcept;

u32 test_system_deinit() noexcept;



#define TEST_THREADPROC_GET_ARGS_AS(...) static_cast<__VA_ARGS__*>(arg)

#define TEST_TBD do { log(LogLevel::Info, "[%s:%d] Test case '%s' has not been implemented yet\n", __FILE__, __LINE__, __FUNCTION__); return; } while (false)

template<typename T>
inline static constexpr const char8* log_string_impl_() noexcept
{
	using Type = std::remove_cv_t<T>;

	if constexpr (std::is_same_v<Type, bool>)
		return "[%s%d] Check '%s' failed. Aborting test case. ('%d' was %s '%d')\n";
	else if constexpr (std::is_integral_v<Type> && std::is_signed_v<Type>)
		return "[%s%d] Check '%s' failed. Aborting test case. ('%lld' was %s '%lld')\n";
	else if constexpr (std::is_integral_v<Type>)
		return "[%s%d] Check '%s' failed. Aborting test case. ('%llu' was %s '%llu')\n";
	else if constexpr (std::is_same_v<Type, float> || std::is_same_v<Type, double>)
		return "[%s:%d] Check '%s' failed. Aborting test case. ('%llu' was %s '%llu')\n";
	else if constexpr (std::is_pointer_v<Type>)
		return "[%s:%d] Check '%s' failed. Aborting test case. ('%p' was %s '%p')\n";
	else
		static_assert(false);
}

#define ASSERT_BASE_(a_, b_, title_, check_, failure_text_) \
	do { \
		const auto va_ = (a_); \
		const auto vb_ = (b_); \
		if (!(check_)) { \
			log(LogLevel::Failure, log_string_impl_<decltype(va_)>(), \
				__FUNCTION__, \
				__LINE__, \
				title_, \
				va_, \
				failure_text_, \
				vb_ \
			); \
			__debugbreak(); \
			add_error(); \
			return; \
		} \
	} while (false)



#define CHECK_EQ(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ == vb_, "not equal to")

#define CHECK_NE(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ != vb_, "equal to")

#define CHECK_LT(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ < vb_, "not less than")

#define CHECK_LE(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ <= vb_, "greater than")

#define CHECK_GT(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ > vb_, "not greater than")

#define CHECK_GE(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ >= vb_, "less than")

#define CHECK_RANGES_EQ(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_.count() == vb_.count() && memcmp(va_.begin(), vb_.begin(), va_.count()) == 0, "not equal to")



#define RAIIWRAPPER(type_, desctructor_) struct RAII##type_##_IMPL_ { type_ t{}; ~RAII##type_##_IMPL_() noexcept { t.desctructor_(); } }

#endif // HELPERS_INCLUDE_GUARD
