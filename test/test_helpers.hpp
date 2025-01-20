#ifndef TEST_HELPERS_INCLUDE_GUARD
#define TEST_HELPERS_INCLUDE_GUARD

#include <intrin.h>
#include <cstring>
#include <vector>

#include "../infra/common.hpp"
#include "../infra/minos.hpp"

struct TestTime
{
	const char8* test;

	const char8* module;

	u64 duration;
};

extern u32 g_failure_count;

extern const char8* g_curr_module;

extern std::vector<TestTime> g_test_times;

extern std::vector<TestTime> g_module_times;

#define TEST_BEGIN \
		const u64 test_start_ = minos::exact_timestamp()

#define TEST_END \
		ASSERT_OR_IGNORE(g_curr_module != nullptr); \
		g_test_times.push_back({ __FUNCTION__, g_curr_module, minos::exact_timestamp() - test_start_ })

#define TEST_MODULE_BEGIN \
		g_curr_module = __FUNCTION__; \
		const u64 module_start_ = minos::exact_timestamp()

#define TEST_MODULE_END \
		ASSERT_OR_IGNORE(g_curr_module != nullptr && strcmp(g_curr_module, __FUNCTION__) == 0); \
		g_curr_module = nullptr; \
		g_module_times.push_back({ nullptr, __FUNCTION__, minos::exact_timestamp() - module_start_ })

#define TEST_RELATION_(a, b, relation) \
		do { \
			if (!((a) relation (b))) { \
				fprintf(stderr, "%s: Assertion %s %s %s failed (%s:%u)\n", __FUNCTION__, #a, #relation, #b, __FILE__, __LINE__); \
				g_failure_count += 1; \
				__debugbreak(); \
			} \
		} while (false)

#define TEST_FUNCTION_(a, b, c, function, relation, expected) \
		do { \
			if ((function((a), (b), (c))) relation (expected)) { \
				fprintf(stderr, "%s: Assertion %s(%s, %s, %s) %s %s failed (%s:%u)\n", __FUNCTION__, #function, #a, #b, #c, #relation, #expected, __FILE__, __LINE__); \
				g_failure_count += 1; \
				__debugbreak(); \
			} \
		} while (false)

#define TEST_EQUAL(a, b) TEST_RELATION_(a, b, ==)

#define TEST_UNEQUAL(a, b) TEST_RELATION_(a, b, !=)

#define TEST_MEM_EQUAL(a, b, bytes) TEST_FUNCTION_(a, b, bytes, memcmp, ==, 0)

#endif // TEST_HELPERS_INCLUDE_GUARD
