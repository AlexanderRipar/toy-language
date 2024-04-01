#include "tests.hpp"
#include "helpers.hpp"
#include "../threading.hpp"
#include "../range.hpp"
#include "../hash.hpp"

#include <cstdlib>
#include <atomic>

namespace ringbuffer_tests
{

}

TESTCASE(ringbuffer)
{
	TEST_TBD;
}

namespace indexstacklist_tests
{

}

TESTCASE(indexstacklist)
{
	TEST_TBD;
}

namespace stridedindexstacklist_tests
{
	struct Node
	{
		u32 data;

		u32 next;
	};

	struct PushThreadProcArgs
	{
		u32 node_count_per_thread;

		Node* nodes;
		
		ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;
	};

	struct PopThreadProcArgs
	{
		std::atomic<u32> popped_node_count;

		Node* nodes;

		ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;
	};

	struct PushAndPopThreadProcArgs
	{
		u32 iteration_count;

		Node* nodes;

		ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack; 
	};

	TESTCASE_THREADPROC(push_parallel_threadproc)
	{
		TEST_INIT;

		PushThreadProcArgs* const args = TEST_THREADPROC_GET_ARGS_AS(PushThreadProcArgs);

		for (u32 i = thread_id * args->node_count_per_thread; i != (thread_id + 1) * args->node_count_per_thread; ++i)
			args->stack.push(args->nodes, sizeof(*args->nodes), i);

		TEST_RETURN;
	}

	TESTCASE_THREADPROC(pop_parallel_threadproc)
	{
		TEST_INIT;

		PopThreadProcArgs* const args = TEST_THREADPROC_GET_ARGS_AS(PopThreadProcArgs);

		u32 popped_node_count = 0;

		while (args->stack.pop(args->nodes, sizeof(Node)) != nullptr)
			popped_node_count += 1;

		args->popped_node_count.fetch_add(popped_node_count, std::memory_order_relaxed);

		TEST_RETURN;
	}

	TESTCASE_THREADPROC(push_and_pop_parallel_threadproc)
	{
		TEST_INIT;

		Node* popped_list = nullptr;

		PushAndPopThreadProcArgs* const args = TEST_THREADPROC_GET_ARGS_AS(PushAndPopThreadProcArgs);

		for (u32 iter = 0; iter != args->iteration_count; ++iter)
		{
			while (true)
			{
				Node* const popped = args->stack.pop(args->nodes, sizeof(Node));

				if (popped == nullptr)
					break;

				popped->next = static_cast<u32>(popped_list == nullptr ? ~0u : popped_list - args->nodes);

				popped_list = popped;
			}

			while (popped_list != nullptr)
			{
				const u32 popped_next = popped_list->next;

				args->stack.push(args->nodes, sizeof(Node), static_cast<u32>(popped_list - args->nodes));

				popped_list = popped_next == ~0u ? nullptr : args->nodes + popped_next;
			}
		}

		TEST_RETURN;
	}

	namespace exclusive
	{
		TESTCASE(pop_on_empty_list_returns_null)
		{
			TEST_INIT;

			ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node dummy_node[1];

			stack.init();

			CHECK_EQ(stack.pop(dummy_node, sizeof(*dummy_node)), nullptr, "Popping an empty stack returns nullptr");

			CHECK_EQ(stack.pop(dummy_node, sizeof(*dummy_node)), nullptr, "Popping an empty stack a second time still returns nullptr");

			TEST_RETURN;
		}

		TESTCASE(init_with_array_then_pop_returns_all_elements)
		{
			TEST_INIT;

			ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node nodes[512];

			for (u32 i = 0; i != array_count(nodes); ++i)
				nodes[i].data = i;

			stack.init(nodes, sizeof(Node), static_cast<u32>(array_count(nodes)));

			u32 popped_node_count = 0;

			while (true)
			{
				const Node* node = stack.pop(nodes, sizeof(Node));

				if (node == nullptr)
					break;

				CHECK_EQ(node->data, popped_node_count, "init with array initializes the stack with the array's first element on top");

				popped_node_count += 1;
			}

			CHECK_EQ(popped_node_count, array_count(nodes), "Expected number of nodes are popped after init with array");

			TEST_RETURN;
		}

		TESTCASE(init_with_double_stride_then_pop_returns_every_second_element)
		{
			TEST_INIT;

			ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node nodes[512];

			constexpr u32 ADJ_COUNT = static_cast<u32>(array_count(nodes) / 2);

			constexpr u32 STRIDE = sizeof(Node) * 2;

			for (u32 i = 0; i != array_count(nodes); ++i)
				nodes[i].data = i;

			stack.init(nodes, STRIDE, ADJ_COUNT);

			u32 popped_node_count = 0;

			while (true)
			{
				const Node* node = stack.pop(nodes, STRIDE);

				if (node == nullptr)
					break;

				CHECK_EQ(node->data, popped_node_count * 2, "init with array and doubled stride initializes the stack with every other element and the array's first element on top");

				popped_node_count += 1;
			}

			CHECK_EQ(popped_node_count, ADJ_COUNT, "Expected number of nodes are popped after init with array and doubled stride");

			TEST_RETURN;
		}

		TESTCASE(push_then_pop_returns_pushed_element)
		{
			TEST_INIT;

			ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node node[1];

			stack.init();

			stack.push(node, sizeof(node), 0);

			CHECK_EQ(stack.pop(node, sizeof(*node)), node, "Pop returns previously pushed element");

			CHECK_EQ(stack.pop(node, sizeof(*node)), nullptr, "Pop after popping all elements returns nullptr");

			TEST_RETURN;
		}

		TESTCASE(push_unsafe_then_pop_returns_pushed_element)
		{
			TEST_INIT;

			ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node node[1];

			stack.init();

			stack.push_unsafe(node, sizeof(Node), 0);

			CHECK_EQ(stack.pop(node, sizeof(*node)), node, "Pop returns previously (unsafely) pushed element");

			CHECK_EQ(stack.pop(node, sizeof(*node)), nullptr, "Pop after popping all elements returns nullptr");

			TEST_RETURN;
		}

		TESTCASE(push_then_pop_unsafe_returns_pushed_element)
		{
			TEST_INIT;

			ThreadsafeStridedIndexStackListHeader<Node, offsetof(Node, next)> stack;

			Node node[1];

			stack.init();

			stack.push(node, sizeof(Node), 0);

			CHECK_EQ(stack.pop_unsafe(node, sizeof(*node)), node, "Pop returns previously (unsafely) pushed element");

			CHECK_EQ(stack.pop_unsafe(node, sizeof(*node)), nullptr, "Pop after popping all elements returns nullptr");

			TEST_RETURN;
		}
	}

	namespace parallel
	{
		TESTCASE(push_does_not_loose_nodes)
		{
			TEST_INIT;

			static constexpr u32 NODE_COUNT_PER_THREAD = 65536;

			static constexpr u32 THREAD_COUNT = 8;

			PushThreadProcArgs args;

			args.node_count_per_thread = NODE_COUNT_PER_THREAD;

			args.stack.init();

			REQUIRE_NE(args.nodes = static_cast<Node*>(malloc(NODE_COUNT_PER_THREAD * THREAD_COUNT * sizeof(Node))), nullptr, "malloc succeeds");

			RUN_TEST_WITH_ARGS(run_on_threads_and_wait, THREAD_COUNT, push_parallel_threadproc, &args, 2000);

			u32 pushed_node_count = 0;

			while (args.stack.pop(args.nodes, sizeof(Node)) != nullptr)
				pushed_node_count += 1;

			CHECK_EQ(pushed_node_count, NODE_COUNT_PER_THREAD * THREAD_COUNT, "Number of sequentially popped nodes is equal to nodes pushed in parallel");

			free(args.nodes);

			TEST_RETURN;
		}

		TESTCASE(pop_does_not_duplicate_nodes)
		{
			TEST_INIT;

			static constexpr u32 NODE_COUNT_PER_THREAD = 65536;

			static constexpr u32 THREAD_COUNT = 8;

			PopThreadProcArgs args;

			args.popped_node_count.store(0, std::memory_order_relaxed);

			args.stack.init();

			REQUIRE_NE(args.nodes = static_cast<Node*>(malloc(NODE_COUNT_PER_THREAD * THREAD_COUNT * sizeof(Node))), nullptr, "malloc succeeds");

			for (u32 i = 0; i != NODE_COUNT_PER_THREAD * THREAD_COUNT; ++i)
				args.stack.push(args.nodes, sizeof(Node), i);

			RUN_TEST_WITH_ARGS(run_on_threads_and_wait, THREAD_COUNT, pop_parallel_threadproc, &args, 2000);

			CHECK_EQ(args.popped_node_count.load(std::memory_order_relaxed), NODE_COUNT_PER_THREAD * THREAD_COUNT, "Number of sequentially pushed nodes is equal to nodes popped in parallel");

			free(args.nodes);

			TEST_RETURN;
		}

		TESTCASE(push_and_pop_does_not_drop_nodes)
		{
			TEST_INIT;

			static constexpr u32 THREAD_COUNT = 8;

			static constexpr u32 TOTAL_NODE_COUNT = 30'000;

			static constexpr u32 THREAD_ITERATION_COUNT = 10;

			PushAndPopThreadProcArgs args;

			args.iteration_count = THREAD_ITERATION_COUNT;

			CHECK_NE(args.nodes = static_cast<Node*>(malloc(TOTAL_NODE_COUNT * sizeof(Node))), nullptr, "malloc succeeds");

			args.stack.init(args.nodes, sizeof(Node), TOTAL_NODE_COUNT);

			RUN_TEST_WITH_ARGS(run_on_threads_and_wait, THREAD_COUNT, push_and_pop_parallel_threadproc, &args, ~0u);

			u32 popped_node_count = 0;

			while (args.stack.pop(args.nodes, sizeof(Node)) != nullptr)
				popped_node_count += 1;

			CHECK_EQ(popped_node_count, TOTAL_NODE_COUNT, "Popping and re-pushing batches of nodes in parallel does not loose any nodes");

			TEST_RETURN;
		}
	}
}

TESTCASE(stridedindexstacklist)
{
	TEST_INIT;

	using namespace stridedindexstacklist_tests;

	RUN_TEST(exclusive::pop_on_empty_list_returns_null);

	RUN_TEST(exclusive::init_with_array_then_pop_returns_all_elements);

	RUN_TEST(exclusive::init_with_double_stride_then_pop_returns_every_second_element);

	RUN_TEST(exclusive::push_then_pop_returns_pushed_element);

	RUN_TEST(exclusive::push_unsafe_then_pop_returns_pushed_element);

	RUN_TEST(exclusive::push_then_pop_unsafe_returns_pushed_element);

	RUN_TEST(parallel::push_does_not_loose_nodes);

	RUN_TEST(parallel::pop_does_not_duplicate_nodes);

	RUN_TEST(parallel::push_and_pop_does_not_drop_nodes);

	TEST_RETURN;
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

	RUN_TEST(ringbuffer);

	RUN_TEST(indexstacklist);

	RUN_TEST(stridedindexstacklist);

	RUN_TEST(map);

	TEST_RETURN;
}
