#ifndef TEST_HELPERS_INCLUDE_GUARD
#define TEST_HELPERS_INCLUDE_GUARD

#include "../infra/types.hpp"
#include "../infra/minos/minos.hpp"
#include "../infra/print/print.hpp"

#include <cstring>
#include <vector>

struct TestResult
{
	const char8* test;

	const char8* module;

	Range<char8> subtest;

	u64 duration;

	u32 failure_count;
};

extern const char8* g_curr_module;

extern std::vector<TestResult> g_test_times;

extern std::vector<TestResult> g_module_times;

extern bool g_ignore_debugbreaks;

#define TEST_BEGIN \
		const u64 test_start_ = minos::exact_timestamp(); \
		const Range<char8> test_name_ = {}

#define TEST_BEGIN_NAMED(name) \
		const u64 test_start_ = minos::exact_timestamp(); \
		const Range<char8> test_name_ = name

#define TEST_END \
		ASSERT_OR_IGNORE(g_curr_module != nullptr); \
		g_test_times.push_back({ __FUNCTION__, g_curr_module, test_name_, minos::exact_timestamp() - test_start_, 0 })

#define TEST_MODULE_BEGIN \
		g_curr_module = __FUNCTION__; \
		const u64 module_start_ = minos::exact_timestamp()

#define TEST_MODULE_END \
		ASSERT_OR_IGNORE(g_curr_module != nullptr && strcmp(g_curr_module, __FUNCTION__) == 0); \
		g_curr_module = nullptr; \
		g_module_times.push_back({ nullptr, __FUNCTION__, {}, minos::exact_timestamp() - module_start_, 0 })

#define TEST_RELATION_(a, b, relation) \
		do { \
			if (!((a) relation (b))) { \
				print(minos::standard_file_handle(minos::StdFileName::StdErr), "%[]%[]%:\n    Assertion `% % %` failed\n    (%:%)\n", \
					__FUNCTION__, \
					test_name_.begin() == nullptr ? "" : "@", test_name_, \
					#a, #relation, #b, \
					__FILE__, __LINE__ \
				); \
				g_test_times.back().failure_count += 1; \
				if (!g_ignore_debugbreaks) \
					DEBUGBREAK; \
			} \
		} while (false)

#define TEST_FUNCTION_(a, b, c, function, relation, expected) \
		do { \
			if (!((function((a), (b), (c))) relation (expected))) { \
				print(minos::standard_file_handle(minos::StdFileName::StdErr), "%[]%[]%:\n    Assertion `%(%, %, %) % %` failed\n    (%:%)\n", \
					__FUNCTION__, test_name_.begin() == nullptr ? "" : "@", test_name_, \
					#function, #a, #b, #c, #relation, #expected, \
					__FILE__, __LINE__ \
				); \
				g_test_times.back().failure_count += 1; \
				if (!g_ignore_debugbreaks) \
					DEBUGBREAK; \
			} \
		} while (false)

#define TEST_EQUAL(a, b) TEST_RELATION_(a, b, ==)

#define TEST_UNEQUAL(a, b) TEST_RELATION_(a, b, !=)

#define TEST_MEM_EQUAL(a, b, bytes) TEST_FUNCTION_(a, b, bytes, memcmp, ==, 0)

#endif // TEST_HELPERS_INCLUDE_GUARD
