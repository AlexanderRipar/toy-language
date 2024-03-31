#ifndef HELPERS_INCLUDE_GUARD
#define HELPERS_INCLUDE_GUARD

#include "../common.hpp"
#include "../range.hpp"
#include "../minos.hpp"
#include <cstdio>
#include <cstdarg>

using thread_proc = u32 (*) (FILE* out_file, void* arg, u32 thread_id, u32 thread_count);

u32 run_on_threads_and_wait(FILE* out_file, u32 thread_count, thread_proc proc, void* arg, u32 timeout) noexcept;

enum class LogLevel
{
	Info,
	Failure,
};

void log(LogLevel level, FILE* f, const char8* fmt, ...) noexcept;


#define TESTCASE(name_) static u32 name_(FILE* out_file) noexcept

#define TESTCASE_WITH_ARGS(name_, ...) static u32 name_(FILE* out_file, __VA_ARGS__) noexcept

#define TESTCASE_THREADPROC(name_) static u32 name_([[maybe_unused]] FILE* out_file, void* arg, [[maybe_unused]] u32 thread_id, [[maybe_unused]] u32 thread_count) noexcept

#define TEST_THREADPROC_GET_ARGS_AS(...) static_cast<__VA_ARGS__*>(arg)

#define TEST_INIT u32 error_count_ = 0

#define TEST_TBD out_file; return 0

#define TEST_RETURN return error_count_

#define ASSERT_BASE_(a_, b_, title_, check_, failure_text_, is_fatal_) \
	do { \
		const auto va_ = (a_); \
		const auto vb_ = (b_); \
		if (!(check_)) { \
			log(LogLevel::Failure, out_file, "%s (ln %d): Check '%s' failed. %s('%s' was %s '%s')\n", \
				__FUNCTION__, \
				__LINE__, \
				title_, \
				is_fatal_ ? "Aborting test case. " : "", \
				#a_, \
				failure_text_, \
				#b_ \
			); \
			__debugbreak(); \
			error_count_ += 1; \
			if constexpr (is_fatal_) \
				TEST_RETURN; \
		} \
	} while (false)



#define CHECK_EQ(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ == vb_, "not equal to", false)

#define CHECK_NE(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ != vb_, "equal to", false)

#define CHECK_LT(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ < vb_, "not less than", false)

#define CHECK_LE(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ <= vb_, "greater than", false)

#define CHECK_GT(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ > vb_, "not greater than", false)

#define CHECK_GE(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ >= vb_, "less than", false)

#define CHECK_RANGES_EQ(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_.count() == vb_.count() && memcmp(va_.begin(), vb_.begin(), va_.count()) == 0, "not equal to", false)


#define REQUIRE_EQ(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ == vb_, "not equal to", true)

#define REQUIRE_NE(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ != vb_, "equal to", true)

#define REQUIRE_LT(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ < vb_, "not less than", true)

#define REQUIRE_LE(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ <= vb_, "greater than", true)

#define REQUIRE_GT(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ > vb_, "not greater than", true)

#define REQUIRE_GE(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_ >= vb_, "less than", true)

#define REQUIRE_RANGES_EQ(a_, b_, title_) ASSERT_BASE_(a_, b_, title_, va_.count() == vb_.count() && memcmp(va_.begin(), vb_.begin(), va_.count()) == 0, "not equal to", true)



#define RAIIWRAPPER(type_, desctructor_) struct RAII##type_##_IMPL_ { type_ t{}; ~RAII##type_##_IMPL_() noexcept { t.desctructor_(); } }

#define RUN_TEST(name_) do { error_count_ += name_(out_file); } while (false)

#define RUN_TEST_WITH_ARGS(name_, ...) do { error_count_ += name_(out_file, __VA_ARGS__); } while (false)

#endif // HELPERS_INCLUDE_GUARD
