#include "tests.hpp"
#include "helpers.hpp"
#include "../threading.hpp"
#include "../range.hpp"
#include "../hash.hpp"

namespace indexstacklist_tests
{

}

TESTCASE(indexstacklist)
{
	TEST_TBD;
}

namespace stridedindexstacklist_tests
{

}

TESTCASE(stridedindexstacklist)
{
	TEST_TBD;
}

namespace map_tests
{
	template<typename T>
	struct KeyGenerator
	{
		void init(u32 per_thread_insertion_count, bool duplicate_inserts, u32 thread_id) noexcept
		{
			static_assert(false, "KeyGenerator created with unsupported type");
		}
	};

	template<>
	struct KeyGenerator<u32>
	{
		u32 m_curr;

		u32 m_end;

		void init(u32 per_thread_insertion_count, bool duplicate_inserts, u32 thread_id) noexcept
		{
			if (duplicate_inserts)
			{
				m_curr = 0;

				m_end = per_thread_insertion_count;
			}
			else
			{
				m_curr = per_thread_insertion_count * thread_id;

				m_end = per_thread_insertion_count * (thread_id + 1);
			}
		}

		bool has_next() const noexcept
		{
			return m_curr != m_end;
		}

		u32 next() noexcept
		{
			return m_curr++;
		}
	};

	template<>
	struct KeyGenerator<Range<char8>>
	{
		u32 m_curr;

		u32 m_end;

		u32 m_extra_bytes;

		u32 m_unq;

		char8 m_extra[512];

		void init(u32 per_thread_insertion_count, bool duplicate_inserts, u32 thread_id) noexcept
		{
			m_curr = 0;

			m_end = per_thread_insertion_count;

			m_extra_bytes = 0;

			memset(m_extra, 0, sizeof(m_extra));

			m_unq = duplicate_inserts ? 0 : per_thread_insertion_count * thread_id;
		}

		bool has_next() const noexcept
		{
			return m_curr != m_end;
		}

		Range<char8> next() noexcept
		{
			const u32 extra_bytes = m_extra_bytes;

			m_extra_bytes = (extra_bytes + 1) % array_count(m_extra);

			m_curr += 1;

			m_unq += 1;

			return Range{ reinterpret_cast<char8*>(&m_unq), sizeof(m_unq) + extra_bytes };
		}
	};

	template<typename Key, typename Value>
	struct InsertThreadProcArgs
	{
		ThreadsafeMap2<Key, Value>* map;

		u32 insertion_count;

		bool duplicate_insertions;
	};

	static u32 hash_key(u32 key) noexcept
	{
		return fnv1a(Range{ &key, 1 }.as_byte_range());
	}

	static u32 hash_key(Range<char8> key) noexcept
	{
		return fnv1a(key.as_byte_range());
	}

	template<typename Key, typename Value>
	TESTCASE_WITH_ARGS(init_standard_map, ThreadsafeMap2<Key, Value>* out_map, MemoryRegion* out_region)
	{
		TEST_INIT;

		ThreadsafeMap2<Key, Value>::InitInfo info;
		info.thread_count = 16;
		info.map.reserve_count = 1u << 18;
		info.map.initial_commit_count = 1u << 12;
		info.map.max_insertion_distance = 1024;
		info.store.reserve_strides = 1u << 18;
		info.store.per_thread_commit_increment_strides = 1u << 12;
		info.store.per_thread_initial_commit_strides = 1u << 12;

		const u64 required_bytes = out_map->required_bytes(info);

		REQUIRE_EQ(out_region->init(required_bytes), true, "MemoryRegion.init succeeds");

		MemorySubregion memory = out_region->subregion(0, required_bytes);

		REQUIRE_EQ(out_map->init(info, memory), true, "ThreadsafeMap.init succeeds");

		TEST_RETURN;
	}

	template<typename Key, typename Value>
	TESTCASE_THREADPROC(insert_thread_proc)
	{
		TEST_INIT;

		InsertThreadProcArgs<Key, Value>* const args = TEST_THREADPROC_GET_ARGS_AS(InsertThreadProcArgs<Key, Value>);

		KeyGenerator<Key> gen;

		gen.init(args->insertion_count, args->duplicate_insertions, thread_id);

		while (gen.has_next())
		{
			Key key = gen.next();

			const u32 hash = hash_key(key);

			bool is_new;

			const u32 index = args->map->index_from(thread_id, key, hash, &is_new);

			if (!args->duplicate_insertions)
				CHECK_EQ(is_new, true, "ThreadsafeMap.index_from sets *opt_is_new to true when called with a new key");

			bool reinsert_is_new;

			const u32 reinsert_index = args->map->index_from(thread_id, key, hash, &reinsert_is_new);

			// @TODO: This fails *very* sporadically
			CHECK_EQ(reinsert_is_new, false, "ThreadsafeMap.index_from sets *opt_is_new to false when called with a key that has just been inserted");

			CHECK_EQ(index, reinsert_index, "ThreadsafeMap.index_from returns the same index when called with the same key");

			const Value* value = args->map->value_from(index);

			CHECK_EQ(value->equal_to_key(key, hash), true, "ThreadsafeMap.value_from returns the value associated with the correct key");
		}

		KeyGenerator<Key> gen2;

		gen2.init(args->insertion_count, args->duplicate_insertions, thread_id);

		while(gen2.has_next())
		{
			Key key = gen2.next();

			const u32 hash = hash_key(key);

			bool is_new;

			const u32 index = args->map->index_from(thread_id, key, hash, &is_new);

			CHECK_EQ(is_new, false, "ThreadsafeMap.index_from sets *opt_is_new to false when reinserting a key with other insertions inbetween");

			const Value* value = args->map->value_from(index);

			CHECK_EQ(value->equal_to_key(key, hash), true, "ThreadsafeMap.value_from returns the value associated with the correct key");
		}

		TEST_RETURN;
	}

	struct FixedSizeValue
	{
		u32 m_hash;

		u32 m_key;

		u32 m_next;

		void init(u32 key, u32 key_hash) noexcept
		{
			m_hash = key_hash;

			m_key = key;
		}

		static constexpr u32 stride() noexcept
		{
			constexpr u32 stride_ = next_pow2(static_cast<u32>(sizeof(FixedSizeValue)));

			return stride_;
		}

		static u32 get_required_strides([[maybe_unused]] u32 key) noexcept
		{
			return 1;
		}

		u32 get_used_strides() noexcept
		{
			return 1;
		}

		u32 get_hash() const noexcept
		{
			return m_hash;
		}

		bool equal_to_key(u32 key, [[maybe_unused]] u32 key_hash) const noexcept
		{
			return m_key == key;
		}

		void set_next(u32 index) noexcept
		{
			m_next = index;
		}

		u32 get_next() const noexcept
		{
			return m_next;
		}
	};

	struct VariableSizeValue
	{
		u32 m_hash;

		u32 m_next;

		u16 m_key_bytes;

		#pragma warning(push)
		#pragma warning(disable : 4200) // nonstandard extension used: zero-sized array in struct/union
		char8 m_key[];
		#pragma warning(pop)

		void init(Range<char8> key, u32 key_hash) noexcept
		{
			m_hash = key_hash;

			ASSERT_OR_EXIT(key.count() <= UINT16_MAX);

			m_key_bytes = static_cast<u16>(key.count());

			memcpy(m_key, key.begin(), key.count());
		}

		static constexpr u32 stride() noexcept
		{
			return 8;
		}

		static u32 get_required_strides(Range<char8> key) noexcept
		{
			return static_cast<u32>((offsetof(VariableSizeValue, m_key) + key.count() + stride() - 1) / stride());
		}

		u32 get_used_strides() const noexcept
		{
			return static_cast<u32>((offsetof(VariableSizeValue, m_key) + m_key_bytes + stride() - 1) / stride());
		}

		u32 get_hash() const noexcept
		{
			return m_hash;
		}

		bool equal_to_key(Range<char8> key, u32 key_hash) const noexcept
		{
			return m_hash == key_hash && key.count() == m_key_bytes && memcmp(key.begin(), m_key, m_key_bytes) == 0;
		}

		void set_next(u32 index) noexcept
		{
			m_next = index;
		}

		u32 get_next() const noexcept
		{
			return m_next;
		}
	};

	namespace init
	{
		TESTCASE(success_on_normal)
		{
			TEST_INIT;

			ThreadsafeMap2<u32, FixedSizeValue> map;

			decltype(map)::InitInfo info;
			info.thread_count = 16;
			info.map.reserve_count = 1u << 18;
			info.map.initial_commit_count = 1u << 12;
			info.map.max_insertion_distance = 1024;
			info.store.reserve_strides = 1u << 20;
			info.store.per_thread_commit_increment_strides = 1u << 12;
			info.store.per_thread_initial_commit_strides = 1u << 14;

			const u64 required_bytes = map.required_bytes(info);

			MemoryRegion region;

			REQUIRE_EQ(region.init(required_bytes), true, "MemoryRegion.init succeeds");

			MemorySubregion memory = region.subregion(0, required_bytes);

			CHECK_EQ(map.init(info, memory), true, "ThreadsafeMap.init succeeds with medium parameters");

			CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");

			TEST_RETURN;
		}

		TESTCASE(success_on_small)
		{
			TEST_INIT;

			ThreadsafeMap2<u32, FixedSizeValue> map;

			decltype(map)::InitInfo info;
			info.thread_count = 1;
			info.map.reserve_count = 4096;
			info.map.initial_commit_count = 4096;
			info.map.max_insertion_distance = 1024;
			info.store.reserve_strides = 4096;
			info.store.per_thread_commit_increment_strides = 4096;
			info.store.per_thread_initial_commit_strides = 4096;

			const u64 required_bytes = map.required_bytes(info);

			MemoryRegion region;

			REQUIRE_EQ(region.init(required_bytes), true, "MemoryRegion.init succeeds");

			MemorySubregion memory = region.subregion(0, required_bytes);

			CHECK_EQ(map.init(info, memory), true, "ThreadsafeMap.init succeeds with small parameters");

			CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");

			TEST_RETURN;
		}

		TESTCASE(success_on_large)
		{
			TEST_INIT;

			ThreadsafeMap2<u32, FixedSizeValue> map;

			decltype(map)::InitInfo info;
			info.thread_count = 1024;
			info.map.reserve_count = 1u << 31;
			info.map.initial_commit_count = 1u << 20;
			info.map.max_insertion_distance = 1024;
			info.store.reserve_strides = 1u << 31;
			info.store.per_thread_commit_increment_strides = 1u << 16;
			info.store.per_thread_initial_commit_strides = 1u << 16;

			const u64 required_bytes = map.required_bytes(info);

			MemoryRegion region;

			REQUIRE_EQ(region.init(required_bytes), true, "MemoryRegion.init succeeds");

			MemorySubregion memory = region.subregion(0, required_bytes);

			CHECK_EQ(map.init(info, memory), true, "ThreadsafeMap.init succeeds with large parameters");

			CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");

			TEST_RETURN;
		}
	}

	namespace exclusive
	{
		namespace fixed_length
		{
			TESTCASE(insert_single)
			{
				TEST_INIT;

				ThreadsafeMap2<u32, FixedSizeValue> map;

				decltype(map)::InitInfo info;
				info.thread_count = 16;
				info.map.reserve_count = 1u << 18;
				info.map.initial_commit_count = 1u << 12;
				info.map.max_insertion_distance = 1024;
				info.store.reserve_strides = 1u << 18;
				info.store.per_thread_commit_increment_strides = 1u << 12;
				info.store.per_thread_initial_commit_strides = 1u << 12;

				const u64 required_bytes = map.required_bytes(info);

				MemoryRegion region;

				REQUIRE_EQ(region.init(required_bytes), true, "MemoryRegion.init succeeds");

				MemorySubregion memory = region.subregion(0, required_bytes);

				REQUIRE_EQ(map.init(info, memory), true, "ThreadsafeMap.init succeeds");

				const u32 key = 0xFEEDBEEF;

				bool is_new1;

				const u32 index1 = map.index_from(0, key, hash_key(key), &is_new1);

				bool is_new2;

				const u32 index2 = map.index_from(0, key, hash_key(key), &is_new2);

				CHECK_EQ(map.value_from(index1)->m_key, key, "ThreadsafeMap.value_from returns the correct value");

				CHECK_EQ(index1, index2, "ThreadsafeMap.index_from called with the same key returns the same index");

				CHECK_EQ(is_new1, true, "ThreadsafeMap.value_from sets *opt_is_new to true on the first insertion of a key");

				CHECK_EQ(is_new2, false, "ThreadsafeMap.value_from sets *opt_is_new to false on the insertion of pre-existing key");

				CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");

				TEST_RETURN;
			}

			TESTCASE(insert_multiple)
			{
				TEST_INIT;

				ThreadsafeMap2<u32, FixedSizeValue> map;

				MemoryRegion region;

				RUN_TEST_WITH_ARGS(init_standard_map, &map, &region);

				InsertThreadProcArgs<u32, FixedSizeValue> args;

				args.map = &map;
				args.duplicate_insertions = false;
				args.insertion_count = 200'000;

				RUN_TEST_WITH_ARGS(run_on_threads_and_wait, 1, insert_thread_proc<u32, FixedSizeValue>, &args, 2000);

				CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");

				TEST_RETURN;
			}
		}

		namespace varying_length
		{
			TESTCASE(insert_single)
			{
				TEST_INIT;

				ThreadsafeMap2<Range<char8>, VariableSizeValue> map;

				MemoryRegion region;

				RUN_TEST_WITH_ARGS(init_standard_map, &map, &region);

				InsertThreadProcArgs<Range<char8>, VariableSizeValue> args;

				args.map = &map;
				args.duplicate_insertions = false;
				args.insertion_count = 1;

				RUN_TEST_WITH_ARGS(run_on_threads_and_wait, 1, insert_thread_proc<Range<char8>, VariableSizeValue>, &args, 2000);

				CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");

				TEST_RETURN;
			}

			TESTCASE(insert_multiple)
			{
				TEST_INIT;

				ThreadsafeMap2<Range<char8>, VariableSizeValue> map;

				MemoryRegion region;

				RUN_TEST_WITH_ARGS(init_standard_map, &map, &region);

				InsertThreadProcArgs<Range<char8>, VariableSizeValue> args;

				args.map = &map;
				args.duplicate_insertions = false;
				args.insertion_count = 1'000;

				RUN_TEST_WITH_ARGS(run_on_threads_and_wait, 1, insert_thread_proc<Range<char8>, VariableSizeValue>, &args, 2000);

				CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");

				TEST_RETURN;
			}
		}
	}

	namespace parallel
	{
		namespace fixed_length
		{
			TESTCASE(insert_no_overlap)
			{
				TEST_INIT;

				ThreadsafeMap2<u32, FixedSizeValue> map;

				MemoryRegion region;

				RUN_TEST_WITH_ARGS(init_standard_map, &map, &region);

				InsertThreadProcArgs<u32, FixedSizeValue> args;

				args.map = &map;
				args.duplicate_insertions = false;
				args.insertion_count = 200'000 / 16;

				RUN_TEST_WITH_ARGS(run_on_threads_and_wait, 16, insert_thread_proc<u32, FixedSizeValue>, &args, 2000);

				CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");

				TEST_RETURN;
			}

			TESTCASE(insert_overlap)
			{
				TEST_INIT;

				ThreadsafeMap2<u32, FixedSizeValue> map;

				MemoryRegion region;

				RUN_TEST_WITH_ARGS(init_standard_map, &map, &region);

				InsertThreadProcArgs<u32, FixedSizeValue> args;

				args.map = &map;
				args.duplicate_insertions = true;
				args.insertion_count = 200'000;

				// @TODO: This sometimes fails on the assertion in ThreadsafeMap.release_thread_write_lock with old_write_lock being 0.
				// @TODO: It also sometimes fails the assert that *out_is_new is set to false on reinsertion.
				RUN_TEST_WITH_ARGS(run_on_threads_and_wait, 16, insert_thread_proc<u32, FixedSizeValue>, &args, 2000);

				CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");

				TEST_RETURN;
			}
		}

		namespace varying_length
		{
			TESTCASE(insert_no_overlap)
			{
				TEST_INIT;

				ThreadsafeMap2<Range<char8>, VariableSizeValue> map;

				MemoryRegion region;

				RUN_TEST_WITH_ARGS(init_standard_map, &map, &region);

				InsertThreadProcArgs<Range<char8>, VariableSizeValue> args;

				args.map = &map;
				args.duplicate_insertions = false;
				args.insertion_count = 1000 / 16;

				RUN_TEST_WITH_ARGS(run_on_threads_and_wait, 16, insert_thread_proc<Range<char8>, VariableSizeValue>, &args, 2000);

				CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");

				TEST_RETURN;
			}

			TESTCASE(insert_overlap)
			{
				TEST_INIT;

				ThreadsafeMap2<Range<char8>, VariableSizeValue> map;

				MemoryRegion region;

				RUN_TEST_WITH_ARGS(init_standard_map, &map, &region);

				InsertThreadProcArgs<Range<char8>, VariableSizeValue> args;

				args.map = &map;
				args.duplicate_insertions = true;
				args.insertion_count = 1000;

				RUN_TEST_WITH_ARGS(run_on_threads_and_wait, 16, insert_thread_proc<Range<char8>, VariableSizeValue>, &args, 2000);

				CHECK_EQ(region.deinit(), true, "MemoryRegion.deinit succeeds after successful initialization");

				TEST_RETURN;
			}
		}
	}
}

TESTCASE(map)
{
	TEST_INIT;

	using namespace map_tests;

	RUN_TEST(init::success_on_normal);

	RUN_TEST(init::success_on_small);

	RUN_TEST(init::success_on_large);

	RUN_TEST(exclusive::fixed_length::insert_single);

	RUN_TEST(exclusive::fixed_length::insert_multiple);

	RUN_TEST(exclusive::varying_length::insert_single);

	RUN_TEST(exclusive::varying_length::insert_multiple);

	RUN_TEST(parallel::fixed_length::insert_no_overlap);

	RUN_TEST(parallel::fixed_length::insert_overlap);

	RUN_TEST(parallel::varying_length::insert_no_overlap);

	RUN_TEST(parallel::varying_length::insert_overlap);

	TEST_RETURN;
}

u32 test::threading(FILE* out_file) noexcept
{
	TEST_INIT;

	RUN_TEST(indexstacklist);

	RUN_TEST(stridedindexstacklist);

	RUN_TEST(map);

	TEST_RETURN;
}
