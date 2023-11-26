#ifndef TEST_HELPERS_INCLUDE_GUARD
#define TEST_HELPERS_INCLUDE_GUARD

#include "../common.hpp"
#include "../range.hpp"
#include "../minwin.hpp"
#include <cstdio>
#include <cstdarg>

using thread_proc = u32 (*) (void* arg);

u32 run_on_threads_and_wait(u32 thread_count, thread_proc f, void* arg, u32 timeout) noexcept;

void log_error(FILE* f, const char8* fmt, ...) noexcept;

#define TEST_INIT u32 error_count_ = 0

#define TEST_RETURN return error_count_

#define ASSERT_BASE_(a_, b_, title_, check_, failure_text_) \
	do { \
		const auto va_ = (a_); \
		const auto vb_ = (b_); \
		if (!(check_)) { \
			log_error(out_file, "%s (ln %d): Check '%s' failed. Aborting test case. ('%s' was %s '%s')\n", \
				__FUNCTION__, \
				__LINE__, \
				__FUNCTION__, \
				title_, \
				#a_, \
				failure_text_, \
				#b_ \
			); \
			__debugbreak(); \
			error_count_ += 1; \
			TEST_RETURN; \
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

#define RUN_TEST(expr_) do { error_count_ += (expr_); } while (false)

#endif // TEST_HELPERS_INCLUDE_GUARD
